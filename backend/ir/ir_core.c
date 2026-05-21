#include "backend/ir/ir.h"
#include <string.h>

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

int ir_vreg_valid(ir_vreg_t v)
{
    return v.id != IR_NO_VREG;
}

ir_vreg_t ir_no_vreg(void)
{
    return (ir_vreg_t){
        .id   = IR_NO_VREG,
        .type = IR_TYPE_VOID,
    };
}

i64_t ir_f64_to_bits(double value)
{
    i64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double ir_f64_from_bits(i64_t bits)
{
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int ir_instr_has_dst(const ir_instr_t* in)
{
    return in && ir_vreg_valid(in->dst);
}

int ir_op_is_pure(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_MOV:
        case IR_OP_MOV_IMM_I64:
        case IR_OP_MOV_IMM_F64:
        case IR_OP_LOAD_SLOT:

        case IR_OP_NEG_I64:
        case IR_OP_NOT_I64:
        case IR_OP_NEG_F64:

        case IR_OP_I64_TO_F64:
        case IR_OP_F64_TO_I64:

        case IR_OP_ADD_I64:
        case IR_OP_SUB_I64:
        case IR_OP_MUL_I64:
        case IR_OP_DIV_I64:
        case IR_OP_POW_I64:

        case IR_OP_ADD_F64:
        case IR_OP_SUB_F64:
        case IR_OP_MUL_F64:
        case IR_OP_DIV_F64:
        case IR_OP_POW_F64:

        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:

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
            return 1;

        default:
            return 0;
    }
}

int ir_op_is_binary_value(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_ADD_I64:
        case IR_OP_SUB_I64:
        case IR_OP_MUL_I64:
        case IR_OP_DIV_I64:
        case IR_OP_POW_I64:

        case IR_OP_ADD_F64:
        case IR_OP_SUB_F64:
        case IR_OP_MUL_F64:
        case IR_OP_DIV_F64:
        case IR_OP_POW_F64:

        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:

        case IR_OP_CMP_EQ_F64:
        case IR_OP_CMP_NE_F64:
        case IR_OP_CMP_LT_F64:
        case IR_OP_CMP_GT_F64:
        case IR_OP_CMP_LE_F64:
        case IR_OP_CMP_GE_F64:
            return 1;

        default:
            return 0;
    }
}

int ir_op_is_unary_value(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_NEG_I64:
        case IR_OP_NOT_I64:
        case IR_OP_NEG_F64:

        case IR_OP_I64_TO_F64:
        case IR_OP_F64_TO_I64:

        case IR_OP_FLOOR_F64:
        case IR_OP_CEIL_F64:
        case IR_OP_ROUND_F64:
        case IR_OP_SQRT_F64:
            return 1;

        default:
            return 0;
    }
}

int ir_op_is_nullary_value(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_SCANF_I64:
        case IR_OP_SCANF_F64:
        case IR_OP_GETCHAR_I64:
            return 1;

        default:
            return 0;
    }
}

int ir_op_is_unary_effect(ir_op_t op)
{
    switch (op)
    {
        case IR_OP_PRINTF_I64:
        case IR_OP_PRINTF_F64:
        case IR_OP_PUTCHAR_I64:
            return 1;

        default:
            return 0;
    }
}
