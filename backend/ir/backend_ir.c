#include "backend_ir.h"

err_t ir_module_ctor(ir_module_t* m)
{
    if (!m)
        return ERR_BAD_ARG;

    memset(m, 0, sizeof(*m));
    return OK;
}

void ir_module_dtor(ir_module_t* m)
{
    if (!m)
        return;

    for (size_t i = 0; i < m->func_count; ++i)
        ir_func_dtor(&m->funcs[i]);

    free(m->funcs);
    memset(m, 0, sizeof(*m));
}

err_t ir_func_ctor(ir_func_t* f, size_t name_id, const char* asm_label, ast_type_t ret_type)
{
    if (!f)
        return ERR_BAD_ARG;

    memset(f, 0, sizeof(*f));

    f->name_id   = name_id;
    f->asm_label = asm_label;
    f->ret_type  = ret_type;

    return OK;
}

void ir_func_dtor(ir_func_t* f)
{
    if (!f)
        return;

    free(f->instrs);
    free(f->slots);
    memset(f, 0, sizeof(*f));
}

ir_vreg_t ir_new_vreg(ir_func_t* f, ir_type_t type)
{
    if (!f)
        return (ir_vreg_t){ .id = IR_NO_VREG, .type = IR_TYPE_VOID };

    return (ir_vreg_t){
        .id   = f->vreg_count++,
        .type = type,
    };
}

size_t ir_new_label(ir_func_t* f)
{
    if (!f)
        return IR_NO_LABEL;

    return f->label_count++;
}

err_t ir_add_slot(ir_func_t* f, size_t name_id, ast_type_t type, size_t* out_slot)
{
    if (!f)
        return ERR_BAD_ARG;

    BE_VEC_GROW(f->slots, f->slot_cap, f->slot_count + 1, ir_slot_t);

    size_t slot = f->slot_count++;

    f->slots[slot] = (ir_slot_t){
        .name_id = name_id,
        .type    = type,
        .slot    = slot,
    };

    if (out_slot)
        *out_slot = slot;

    return OK;
}

err_t ir_emit(ir_func_t* f, ir_instr_t in)
{
    if (!f)
        return ERR_BAD_ARG;

    BE_VEC_GROW(f->instrs, f->instr_cap, f->instr_count + 1, ir_instr_t);

    f->instrs[f->instr_count++] = in;
    return OK;
}

const char* ir_op_to_cstr(ir_op_t op)
{
    switch (op)
    {
#define X(name) case IR_OP_##name: return #name;
        IR_OP_LIST(X)
#undef X

        default:
            return "<?IR_OP>";
    }
}

void ir_alloc_dtor(ir_alloc_t* a)
{
    if (!a)
        return;

    free(a->intervals);
    free(a->vreg_locs);
    memset(a, 0, sizeof(*a));
}

static int ir_vreg_valid_(ir_vreg_t v)
{
    return v.id != IR_NO_VREG && v.type != IR_TYPE_VOID;
}

static ir_vreg_t ir_no_vreg_(void)
{
    return (ir_vreg_t){
        .id   = IR_NO_VREG,
        .type = IR_TYPE_VOID,
    };
}

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

    if (!ir_vreg_valid_(v))
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

            case IR_OP_MOV:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = mov ");
                ir_dump_vreg_(out, in->a);
                break;

            case IR_OP_MOV_IMM_I64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = imm_i64 %lld", (long long)in->imm);
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

            case IR_OP_NEG_I64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = neg_i64 ");
                ir_dump_vreg_(out, in->a);
                break;

            case IR_OP_NOT_I64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = not_i64 ");
                ir_dump_vreg_(out, in->a);
                break;

            case IR_OP_ADD_I64:
            case IR_OP_SUB_I64:
            case IR_OP_MUL_I64:
            case IR_OP_DIV_I64:
            case IR_OP_CMP_EQ_I64:
            case IR_OP_CMP_NE_I64:
            case IR_OP_CMP_LT_I64:
            case IR_OP_CMP_GT_I64:
            case IR_OP_CMP_LE_I64:
            case IR_OP_CMP_GE_I64:
                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = %s ", ir_op_to_cstr(in->op));
                ir_dump_vreg_(out, in->a);
                fprintf(out, ", ");
                ir_dump_vreg_(out, in->b);
                break;

            case IR_OP_CALL:
            {
                const char* cname = ast_name_cstr(tree, in->func_id);

                ir_dump_vreg_(out, in->dst);
                fprintf(out, " = call %s(", cname ? cname : "<?>");

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

                if (ir_vreg_valid_(in->a))
                {
                    fprintf(out, " ");
                    ir_dump_vreg_(out, in->a);
                }
                break;

            case IR_OP_PRINTF_I64:
                fprintf(out, "printf_i64 ");
                ir_dump_vreg_(out, in->a);
                break;

            default:
                fprintf(out, "<?op %d>", (int)in->op);
                break;
        }

        fprintf(out, "\n");
    }
}

typedef struct
{
    int   known;
    i64_t value;
} ir_const_i64_t;

static int ir_instr_has_dst_(const ir_instr_t* in)
{
    return in && ir_vreg_valid_(in->dst);
}

static int ir_op_is_pure_(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_MOV:
        case IR_OP_MOV_IMM_I64:
        case IR_OP_LOAD_SLOT:

        case IR_OP_NEG_I64:
        case IR_OP_NOT_I64:

        case IR_OP_ADD_I64:
        case IR_OP_SUB_I64:
        case IR_OP_MUL_I64:
        case IR_OP_DIV_I64:

        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:

        case IR_OP_MOV_IMM_F64:
        case IR_OP_NEG_F64:
        case IR_OP_I64_TO_F64:
        case IR_OP_F64_TO_I64:
        case IR_OP_ADD_F64:
        case IR_OP_SUB_F64:
        case IR_OP_MUL_F64:
        case IR_OP_DIV_F64:
        case IR_OP_CMP_EQ_F64:
        case IR_OP_CMP_NE_F64:
        case IR_OP_CMP_LT_F64:
        case IR_OP_CMP_GT_F64:
        case IR_OP_CMP_LE_F64:
        case IR_OP_CMP_GE_F64:
        case IR_OP_FLOOR_F64:
        case IR_OP_CEIL_F64:
        case IR_OP_ROUND_F64:
        case IR_OP_SQRT_F64:
        case IR_OP_POW_F64:
        case IR_OP_POW_I64:
            return 1;

        default:
            return 0;
    }
}

static int ir_eval_unary_i64_(ir_op_t op, i64_t a, i64_t* out)
{
    if (!out)
        return 0;

    switch (op)
    {
        case IR_OP_NEG_I64:
            *out = -a;
            return 1;

        case IR_OP_NOT_I64:
            *out = !a;
            return 1;

        default:
            return 0;
    }
}

static int ir_eval_binary_i64_(ir_op_t op, i64_t a, i64_t b, i64_t* out)
{
    if (!out)
        return 0;

    switch (op)
    {
        case IR_OP_ADD_I64:
            *out = a + b;
            return 1;

        case IR_OP_SUB_I64:
            *out = a - b;
            return 1;

        case IR_OP_MUL_I64:
            *out = a * b;
            return 1;

        case IR_OP_DIV_I64:
            if (b == 0)
                return 0;

            *out = a / b;
            return 1;

        case IR_OP_CMP_EQ_I64:
            *out = (a == b);
            return 1;

        case IR_OP_CMP_NE_I64:
            *out = (a != b);
            return 1;

        case IR_OP_CMP_LT_I64:
            *out = (a < b);
            return 1;

        case IR_OP_CMP_GT_I64:
            *out = (a > b);
            return 1;

        case IR_OP_CMP_LE_I64:
            *out = (a <= b);
            return 1;

        case IR_OP_CMP_GE_I64:
            *out = (a >= b);
            return 1;

        default:
            return 0;
    }
}

static int ir_fold_constants_(ir_func_t* f)
{
    if (!f || f->vreg_count == 0)
        return 0;

    ir_const_i64_t* c = (ir_const_i64_t*)calloc(f->vreg_count, sizeof(*c));
    if (!c)
        return 0;

    int changed = 0;

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        ir_instr_t* in = &f->instrs[ip];

        switch (in->op)
        {
            case IR_OP_MOV_IMM_I64:
                if (ir_vreg_valid_(in->dst))
                {
                    c[in->dst.id].known = 1;
                    c[in->dst.id].value = in->imm;
                }
                break;

            case IR_OP_MOV:
                if (ir_vreg_valid_(in->dst))
                {
                    if (ir_vreg_valid_(in->a) && c[in->a.id].known)
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = c[in->a.id].value;
                        in->a   = ir_no_vreg_();

                        c[in->dst.id].known = 1;
                        c[in->dst.id].value = in->imm;

                        changed = 1;
                    }
                    else
                    {
                        c[in->dst.id].known = 0;
                    }
                }
                break;

            case IR_OP_NEG_I64:
            case IR_OP_NOT_I64:
                if (ir_vreg_valid_(in->dst))
                {
                    i64_t val = 0;

                    if (ir_vreg_valid_(in->a) &&
                        c[in->a.id].known &&
                        ir_eval_unary_i64_(in->op, c[in->a.id].value, &val))
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = val;
                        in->a   = ir_no_vreg_();

                        c[in->dst.id].known = 1;
                        c[in->dst.id].value = val;

                        changed = 1;
                    }
                    else
                    {
                        c[in->dst.id].known = 0;
                    }
                }
                break;

            case IR_OP_ADD_I64:
            case IR_OP_SUB_I64:
            case IR_OP_MUL_I64:
            case IR_OP_DIV_I64:
            case IR_OP_CMP_EQ_I64:
            case IR_OP_CMP_NE_I64:
            case IR_OP_CMP_LT_I64:
            case IR_OP_CMP_GT_I64:
            case IR_OP_CMP_LE_I64:
            case IR_OP_CMP_GE_I64:
                if (ir_vreg_valid_(in->dst))
                {
                    i64_t val = 0;

                    if (ir_vreg_valid_(in->a) &&
                        ir_vreg_valid_(in->b) &&
                        c[in->a.id].known &&
                        c[in->b.id].known &&
                        ir_eval_binary_i64_(in->op,
                                            c[in->a.id].value,
                                            c[in->b.id].value,
                                            &val))
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = val;
                        in->a   = ir_no_vreg_();
                        in->b   = ir_no_vreg_();

                        c[in->dst.id].known = 1;
                        c[in->dst.id].value = val;

                        changed = 1;
                    }
                    else
                    {
                        c[in->dst.id].known = 0;
                    }
                }
                break;

            default:
                if (ir_instr_has_dst_(in))
                    c[in->dst.id].known = 0;
                break;
        }
    }

    free(c);
    return changed;
}

static int ir_is_const_vreg_(const ir_const_i64_t* c, ir_vreg_t v, i64_t value)
{
    return ir_vreg_valid_(v) && c[v.id].known && c[v.id].value == value;
}

static void ir_replace_with_mov_(ir_instr_t* in, ir_vreg_t src)
{
    in->op  = IR_OP_MOV;
    in->a   = src;
    in->b   = ir_no_vreg_();
    in->imm = 0;
}

static int ir_simplify_algebra_(ir_func_t* f)
{
    if (!f || f->vreg_count == 0)
        return 0;

    ir_const_i64_t* c = (ir_const_i64_t*)calloc(f->vreg_count, sizeof(*c));
    if (!c)
        return 0;

    int changed = 0;

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        ir_instr_t* in = &f->instrs[ip];

        switch (in->op)
        {
            case IR_OP_MOV_IMM_I64:
                if (ir_vreg_valid_(in->dst))
                {
                    c[in->dst.id].known = 1;
                    c[in->dst.id].value = in->imm;
                }
                break;

            case IR_OP_MOV:
                if (ir_vreg_valid_(in->dst))
                {
                    if (ir_vreg_valid_(in->a) && c[in->a.id].known)
                    {
                        c[in->dst.id].known = 1;
                        c[in->dst.id].value = c[in->a.id].value;
                    }
                    else
                    {
                        c[in->dst.id].known = 0;
                    }
                }
                break;

            case IR_OP_ADD_I64:
                if (ir_is_const_vreg_(c, in->b, 0))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }
                else if (ir_is_const_vreg_(c, in->a, 0))
                {
                    ir_replace_with_mov_(in, in->b);
                    changed = 1;
                }

                if (ir_vreg_valid_(in->dst))
                    c[in->dst.id].known = 0;
                break;

            case IR_OP_SUB_I64:
                if (ir_is_const_vreg_(c, in->b, 0))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }

                if (ir_vreg_valid_(in->dst))
                    c[in->dst.id].known = 0;
                break;

            case IR_OP_MUL_I64:
                if (ir_is_const_vreg_(c, in->b, 1))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }
                else if (ir_is_const_vreg_(c, in->a, 1))
                {
                    ir_replace_with_mov_(in, in->b);
                    changed = 1;
                }
                else if (ir_is_const_vreg_(c, in->b, 0) ||
                         ir_is_const_vreg_(c, in->a, 0))
                {
                    in->op  = IR_OP_MOV_IMM_I64;
                    in->imm = 0;
                    in->a   = ir_no_vreg_();
                    in->b   = ir_no_vreg_();

                    changed = 1;
                }

                if (ir_vreg_valid_(in->dst))
                    c[in->dst.id].known = 0;
                break;

            case IR_OP_DIV_I64:
                if (ir_is_const_vreg_(c, in->b, 1))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }

                if (ir_vreg_valid_(in->dst))
                    c[in->dst.id].known = 0;
                break;

            default:
                if (ir_instr_has_dst_(in))
                    c[in->dst.id].known = 0;
                break;
        }
    }

    free(c);
    return changed;
}

static void ir_count_use_(size_t* uses, size_t n, ir_vreg_t v)
{
    if (!uses)
        return;

    if (!ir_vreg_valid_(v))
        return;

    if (v.id < n)
        uses[v.id]++;
}

static void ir_count_instr_uses_(size_t* uses, size_t n, const ir_instr_t* in)
{
    if (!in)
        return;

    switch (in->op)
    {
        case IR_OP_MOV:
        case IR_OP_STORE_SLOT:
        case IR_OP_NEG_I64:
        case IR_OP_NOT_I64:
        case IR_OP_JZ:
        case IR_OP_RET:
        case IR_OP_PRINTF_I64:
        case IR_OP_PRINTF_F64:
        case IR_OP_PUTCHAR_I64:
        case IR_OP_FLOOR_F64:
        case IR_OP_CEIL_F64:
        case IR_OP_ROUND_F64:
        case IR_OP_I64_TO_F64:
        case IR_OP_F64_TO_I64:
        case IR_OP_NEG_F64:
        case IR_OP_SQRT_F64:
            ir_count_use_(uses, n, in->a);
            break;

        case IR_OP_ADD_I64:
        case IR_OP_SUB_I64:
        case IR_OP_MUL_I64:
        case IR_OP_DIV_I64:
        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:
        case IR_OP_ADD_F64:
        case IR_OP_SUB_F64:
        case IR_OP_MUL_F64:
        case IR_OP_DIV_F64:
        case IR_OP_CMP_EQ_F64:
        case IR_OP_CMP_NE_F64:
        case IR_OP_CMP_LT_F64:
        case IR_OP_CMP_GT_F64:
        case IR_OP_CMP_LE_F64:
        case IR_OP_CMP_GE_F64:
        case IR_OP_POW_F64:
        case IR_OP_POW_I64:
        case IR_OP_JCC_FALSE_I64:
        case IR_OP_JCC_FALSE_F64:
            ir_count_use_(uses, n, in->a);
            ir_count_use_(uses, n, in->b);
            break;

        case IR_OP_CALL:
            for (size_t i = 0; i < in->arg_count; ++i)
                ir_count_use_(uses, n, in->args[i]);
            break;

        case IR_OP_RUNTIME_SET_PIXEL:
            for (size_t i = 0; i < in->arg_count; ++i)
                ir_count_use_(uses, n, in->args[i]);
            break;

        default:
            break;
    }
}

static int ir_remove_dead_temps_once_(ir_func_t* f)
{
    if (!f || f->vreg_count == 0 || f->instr_count == 0)
        return 0;

    size_t* uses = (size_t*)calloc(f->vreg_count, sizeof(*uses));
    if (!uses)
        return 0;

    for (size_t i = 0; i < f->instr_count; ++i)
        ir_count_instr_uses_(uses, f->vreg_count, &f->instrs[i]);

    size_t w = 0;
    int changed = 0;

    for (size_t r = 0; r < f->instr_count; ++r)
    {
        ir_instr_t* in = &f->instrs[r];

        int remove = 0;

        if (ir_op_is_pure_(in->op) &&
            ir_instr_has_dst_(in) &&
            in->dst.id < f->vreg_count &&
            uses[in->dst.id] == 0)
        {
            remove = 1;
        }

        if (remove)
        {
            changed = 1;
            continue;
        }

        if (w != r)
            f->instrs[w] = f->instrs[r];

        w++;
    }

    f->instr_count = w;

    free(uses);
    return changed;
}

static int ir_remove_dead_temps_(ir_func_t* f)
{
    int any = 0;

    for (;;)
    {
        int changed = ir_remove_dead_temps_once_(f);

        if (!changed)
            break;

        any = 1;
    }

    return any;
}

static int ir_remove_trivial_moves_(ir_func_t* f)
{
    if (!f)
        return 0;

    size_t w = 0;
    int changed = 0;

    for (size_t r = 0; r < f->instr_count; ++r)
    {
        ir_instr_t* in = &f->instrs[r];

        if (in->op == IR_OP_MOV &&
            ir_vreg_valid_(in->dst) &&
            ir_vreg_valid_(in->a) &&
            in->dst.id == in->a.id)
        {
            changed = 1;
            continue;
        }

        if (w != r)
            f->instrs[w] = f->instrs[r];

        w++;
    }

    f->instr_count = w;
    return changed;
}

err_t ir_optimize_func(ir_func_t* f)
{
    if (!f)
        return ERR_BAD_ARG;

    for (size_t iter = 0; iter < 8; ++iter)
    {
        int changed = 0;

        changed |= ir_fold_constants_(f);
        changed |= ir_simplify_algebra_(f);
        changed |= ir_fold_constants_(f);
        changed |= ir_remove_trivial_moves_(f);
        changed |= ir_remove_dead_temps_(f);

        if (!changed)
            break;
    }

    return OK;
}

static err_t ir_touch_vreg_(ir_alloc_t* a, ir_vreg_t v, size_t ip)
{
    if (!ir_vreg_valid_(v))
        return OK;

    BE_VEC_GROW(a->intervals, a->interval_cap, v.id + 1, ir_interval_t);

    while (a->interval_count <= v.id)
    {
        a->intervals[a->interval_count] = (ir_interval_t){
            .vreg_id = a->interval_count,
            .first   = SIZE_MAX,
            .last    = 0,
            .type    = IR_TYPE_VOID,
            .loc     = { .kind = IR_LOC_NONE, .preg = -1, .spill_slot = IR_NO_SLOT },
        };

        a->interval_count++;
    }

    ir_interval_t* it = &a->intervals[v.id];

    if (it->first == SIZE_MAX)
        it->first = ip;

    if (it->last < ip)
        it->last = ip;

    it->type = v.type;
    return OK;
}

static err_t ir_collect_intervals_(const ir_func_t* f, ir_alloc_t* a)
{
    if (!f || !a)
        return ERR_BAD_ARG;

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        const ir_instr_t* in = &f->instrs[ip];

        switch (in->op)
        {
            case IR_OP_MOV_IMM_I64:
            case IR_OP_LOAD_SLOT:
            case IR_OP_CALL:
            case IR_OP_MOV_IMM_F64:
            case IR_OP_SCANF_I64:
            case IR_OP_SCANF_F64:
            case IR_OP_GETCHAR_I64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                break;

            case IR_OP_MOV:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_STORE_SLOT:
            case IR_OP_JZ:
            case IR_OP_PRINTF_I64:
            case IR_OP_RET:
            case IR_OP_PRINTF_F64:
            case IR_OP_PUTCHAR_I64:
                if (ir_touch_vreg_(a, in->a, ip) != OK) return ERR_ALLOC;
                break;

            case IR_OP_RUNTIME_SET_PIXEL:
                for (size_t i = 0; i < in->arg_count; ++i)
                    if (ir_touch_vreg_(a, in->args[i], ip) != OK)
                        return ERR_ALLOC;
                break;
            case IR_OP_NEG_I64:
            case IR_OP_NOT_I64:
            case IR_OP_NEG_F64:
            case IR_OP_I64_TO_F64:
            case IR_OP_F64_TO_I64:
            case IR_OP_FLOOR_F64:
            case IR_OP_CEIL_F64:
            case IR_OP_ROUND_F64:
            case IR_OP_SQRT_F64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_ADD_I64:
            case IR_OP_SUB_I64:
            case IR_OP_MUL_I64:
            case IR_OP_DIV_I64:
            case IR_OP_CMP_EQ_I64:
            case IR_OP_CMP_NE_I64:
            case IR_OP_CMP_LT_I64:
            case IR_OP_CMP_GT_I64:
            case IR_OP_CMP_LE_I64:
            case IR_OP_CMP_GE_I64:
            case IR_OP_ADD_F64:
            case IR_OP_SUB_F64:
            case IR_OP_MUL_F64:
            case IR_OP_DIV_F64:
            case IR_OP_CMP_EQ_F64:
            case IR_OP_CMP_NE_F64:
            case IR_OP_CMP_LT_F64:
            case IR_OP_CMP_GT_F64:
            case IR_OP_CMP_LE_F64:
            case IR_OP_CMP_GE_F64:
            case IR_OP_POW_F64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->b, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_JCC_FALSE_I64:
case IR_OP_JCC_FALSE_F64:
    if (ir_touch_vreg_(a, in->a, ip) != OK) return ERR_ALLOC;
    if (ir_touch_vreg_(a, in->b, ip) != OK) return ERR_ALLOC;
    break;

            default:
                break;
        }

        if (in->op == IR_OP_CALL)
        {
            for (size_t i = 0; i < in->arg_count; ++i)
                if (ir_touch_vreg_(a, in->args[i], ip) != OK)
                    return ERR_ALLOC;
        }
    }

    return OK;
}

static int interval_cmp_first_(const void* pa, const void* pb)
{
    const ir_interval_t* a = (const ir_interval_t*)pa;
    const ir_interval_t* b = (const ir_interval_t*)pb;

    if (a->first < b->first) return -1;
    if (a->first > b->first) return  1;
    return 0;
}

static void expire_old_(ir_interval_t** active, size_t* active_count, size_t cur_first, int free_regs[], size_t* free_count)
{
    size_t w = 0;

    for (size_t i = 0; i < *active_count; ++i)
    {
        ir_interval_t* it = active[i];

        if (it->last < cur_first)
            free_regs[(*free_count)++] = it->loc.preg;
        else
            active[w++] = it;
    }

    *active_count = w;
}

static size_t find_active_farthest_(ir_interval_t** active, size_t active_count)
{
    size_t best = 0;

    for (size_t i = 1; i < active_count; ++i)
        if (active[i]->last > active[best]->last)
            best = i;

    return best;
}

err_t ir_alloc_run_linear_scan(const ir_func_t* f, ir_alloc_t* out)
{
    if (!f || !out)
        return ERR_BAD_ARG;

    memset(out, 0, sizeof(*out));

    err_t rc = ir_collect_intervals_(f, out);
    if (rc != OK)
        return rc;

    if (out->interval_count == 0)
        return OK;

    ir_interval_t* sorted = (ir_interval_t*)calloc(out->interval_count, sizeof(*sorted));
    if (!sorted)
        return ERR_ALLOC;

    memcpy(sorted, out->intervals, out->interval_count * sizeof(*sorted));
    qsort(sorted, out->interval_count, sizeof(*sorted), interval_cmp_first_);

    ir_interval_t** active = (ir_interval_t**)calloc(out->interval_count, sizeof(*active));
    if (!active)
    {
        free(sorted);
        return ERR_ALLOC;
    }

    int free_regs[NASM_PREG_COUNT] = { 0 };

    for (int i = 0; i < NASM_PREG_COUNT; ++i)
        free_regs[i] = i;

    size_t free_count = NASM_PREG_COUNT;
    size_t active_count            = 0;

    for (size_t si = 0; si < out->interval_count; ++si)
    {
        ir_interval_t* cur = &out->intervals[sorted[si].vreg_id];

        if (cur->first == SIZE_MAX)
            continue;

        expire_old_(active, &active_count, cur->first, free_regs, &free_count);

        if (free_count > 0)
        {
            cur->loc.kind = IR_LOC_REG;
            cur->loc.preg = free_regs[--free_count];

            active[active_count++] = cur;
            continue;
        }

        size_t far_i = find_active_farthest_(active, active_count);
        ir_interval_t* far = active[far_i];

        if (far->last > cur->last)
        {
            cur->loc = far->loc;

            far->loc.kind = IR_LOC_SPILL;
            far->loc.preg = -1;
            far->loc.spill_slot = out->spill_count++;

            active[far_i] = cur;
        }
        else
        {
            cur->loc.kind = IR_LOC_SPILL;
            cur->loc.preg = -1;
            cur->loc.spill_slot = out->spill_count++;
        }
    }

    out->vreg_loc_count = out->interval_count;
    out->vreg_locs = (ir_alloc_loc_t*)calloc(out->vreg_loc_count, sizeof(*out->vreg_locs));
    if (!out->vreg_locs)
    {
        free(sorted);
        free(active);
        return ERR_ALLOC;
    }

    for (size_t i = 0; i < out->interval_count; ++i)
        out->vreg_locs[i] = out->intervals[i].loc;

    free(sorted);
    free(active);
    return OK;
}
