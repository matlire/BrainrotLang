#include "backend/ir/ir.h"

static const char* ir_type_to_cstr_(ir_type_t type)
{
    switch (type)
    {
        case IR_TYPE_VOID: return "void";
        case IR_TYPE_I64:  return "i64";
        case IR_TYPE_F64:  return "f64";
        case IR_TYPE_PTR:  return "ptr";
        default:           return "<?type>";
    }
}

static void ir_dump_vreg_(FILE* out, ir_vreg_t v)
{
    if (!out)
        return;

    if (!ir_vreg_valid(v))
    {
        fprintf(out, "_");
        return;
    }

    fprintf(out, "v%zu:%s", v.id, ir_type_to_cstr_(v.type));
}

static const char* ir_slot_name_(const ast_tree_t* tree, const ir_func_t* f, size_t slot)
{
    if (!tree || !f || slot >= f->slot_count)
        return NULL;

    return ast_name_cstr(tree, f->slots[slot].name_id);
}

static void ir_dump_slot_(FILE* out, const ast_tree_t* tree, const ir_func_t* f, size_t slot)
{
    const char* name = ir_slot_name_(tree, f, slot);

    if (name)
        fprintf(out, "slot%zu(%s)", slot, name);
    else
        fprintf(out, "slot%zu", slot);
}

void ir_dump_func(FILE* out, const ast_tree_t* tree, const ir_func_t* f)
{
    if (!out || !f)
        return;

    const char* fname = ast_name_cstr(tree, f->name_id);

    fprintf(out, "\n;; IR function %s / %s ret=%s\n",
            fname ? fname : "<?fn>",
            f->asm_label ? f->asm_label : "<?label>",
            ast_type_to_cstr(f->ret_type));

    fprintf(out, ";; slots=%zu vregs=%zu labels=%zu instrs=%zu\n",
            f->slot_count,
            f->vreg_count,
            f->label_count,
            f->instr_count);

    for (size_t i = 0; i < f->slot_count; ++i)
    {
        const char* sname = ast_name_cstr(tree, f->slots[i].name_id);

        fprintf(out, ";;   slot%zu name=%s type=%s\n",
                i,
                sname ? sname : "<?>",
                ast_type_to_cstr(f->slots[i].type));
    }

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        const ir_instr_t* in = &f->instrs[ip];
        const char* opname = ir_op_to_cstr(in->op);

        fprintf(out, "%4zu: ", ip);

        switch (in->op)
        {
            case IR_OP_NOP:
                fprintf(out, "nop");
                break;

            case IR_OP_LABEL:
                fprintf(out, ".L%zu:", in->label_id);
                break;

            case IR_OP_JMP:
                fprintf(out, "jmp .L%zu", in->label_id);
                break;

            case IR_OP_JZ:
                fprintf(out, "jz ");
                ir_dump_vreg_(out, in->a);
                fprintf(out, ", .L%zu", in->label_id);
                break;

            case IR_OP_JCC_FALSE_I64:
            case IR_OP_JCC_FALSE_F64:
                fprintf(out, "%s cond_op=%s, ",
                        opname,
                        token_kind_to_cstr((token_kind_t)in->imm));
                ir_dump_vreg_(out, in->a);
                fprintf(out, ", ");
                ir_dump_vreg_(out, in->b);
                fprintf(out, ", .L%zu", in->label_id);
                break;

            case IR_OP_MOV:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = mov ");
                ir_dump_vreg_(out, in->a);
                break;

            case IR_OP_MOV_IMM_I64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = imm_i64 %lld", (long long)in->imm);
                break;

            case IR_OP_MOV_IMM_F64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = imm_f64 %.17g", ir_f64_from_bits(in->imm));
                break;

            case IR_OP_LOAD_SLOT:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = load ");
                ir_dump_slot_(out, tree, f, in->slot);
                break;

            case IR_OP_STORE_SLOT:
                fprintf(out, "store ");
                ir_dump_slot_(out, tree, f, in->slot);
                fprintf(out, ", ");
                ir_dump_vreg_(out, in->a);
                break;

            case IR_OP_ADD_SLOT_IMM_I64:
                fprintf(out, "add_slot_imm_i64 ");
                ir_dump_slot_(out, tree, f, in->slot);
                fprintf(out, ", %lld", (long long)in->imm);
                break;

            case IR_OP_CALL:
            {
                const char* cname = ast_name_cstr(tree, in->func_id);

                if (ir_vreg_valid(in->dst))
                {
                    ir_dump_vreg_(out, in->dst);
                    fprintf(out, " = ");
                }

                fprintf(out, "call %s(", cname ? cname : "<?>");

                for (size_t i = 0; i < in->arg_count; ++i)
                {
                    if (i != 0)
                        fprintf(out, ", ");

                    ir_dump_vreg_(out, in->args[i]);
                }

                fprintf(out, ")");
                break;
            }

            case IR_OP_RET:
                fprintf(out, "ret");

                if (ir_vreg_valid(in->a))
                {
                    fprintf(out, " ");
                    ir_dump_vreg_(out, in->a);
                }
                break;

            case IR_OP_RET_IMM_I64:
                fprintf(out, "ret_imm_i64 %lld", (long long)in->imm);
                break;

            case IR_OP_RUNTIME_DRAW:
                fprintf(out, "runtime_draw");
                break;

            case IR_OP_RUNTIME_CLEAN:
                fprintf(out, "runtime_clean");
                break;

            case IR_OP_RUNTIME_SET_PIXEL:
                fprintf(out, "runtime_set_pixel(");

                for (size_t i = 0; i < in->arg_count; ++i)
                {
                    if (i != 0)
                        fprintf(out, ", ");

                    ir_dump_vreg_(out, in->args[i]);
                }

                fprintf(out, ")");
                break;

            default:
                if (ir_op_is_binary_value(in->op))
                {
                    ir_dump_vreg_(out, in->dst);
                    fprintf(out, " = %s ", opname);
                    ir_dump_vreg_(out, in->a);
                    fprintf(out, ", ");
                    ir_dump_vreg_(out, in->b);
                }
                else if (ir_op_is_unary_value(in->op))
                {
                    ir_dump_vreg_(out, in->dst);
                    fprintf(out, " = %s ", opname);
                    ir_dump_vreg_(out, in->a);
                }
                else if (ir_op_is_nullary_value(in->op))
                {
                    ir_dump_vreg_(out, in->dst);
                    fprintf(out, " = %s()", opname);
                }
                else if (ir_op_is_unary_effect(in->op))
                {
                    fprintf(out, "%s ", opname);
                    ir_dump_vreg_(out, in->a);
                }
                else
                {
                    fprintf(out, "<?op %d %s>", (int)in->op, opname);
                }
                break;
        }

        fprintf(out, "\n");
    }
}
