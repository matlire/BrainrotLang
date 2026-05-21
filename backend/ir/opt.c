#include "backend/ir/ir.h"

typedef struct
{
    int   known;
    i64_t value;
} ir_const_i64_t;

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
                if (ir_vreg_valid(in->dst))
                {
                    c[in->dst.id].known = 1;
                    c[in->dst.id].value = in->imm;
                }
                break;

            case IR_OP_MOV:
                if (ir_vreg_valid(in->dst))
                {
                    if (ir_vreg_valid(in->a) && c[in->a.id].known)
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = c[in->a.id].value;
                        in->a   = ir_no_vreg();

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
                if (ir_vreg_valid(in->dst))
                {
                    i64_t val = 0;

                    if (ir_vreg_valid(in->a) &&
                        c[in->a.id].known &&
                        ir_eval_unary_i64_(in->op, c[in->a.id].value, &val))
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = val;
                        in->a   = ir_no_vreg();

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
                if (ir_vreg_valid(in->dst))
                {
                    i64_t val = 0;

                    if (ir_vreg_valid(in->a) &&
                        ir_vreg_valid(in->b) &&
                        c[in->a.id].known &&
                        c[in->b.id].known &&
                        ir_eval_binary_i64_(in->op,
                                            c[in->a.id].value,
                                            c[in->b.id].value,
                                            &val))
                    {
                        in->op  = IR_OP_MOV_IMM_I64;
                        in->imm = val;
                        in->a   = ir_no_vreg();
                        in->b   = ir_no_vreg();

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
                if (ir_instr_has_dst(in))
                    c[in->dst.id].known = 0;
                break;
        }
    }

    free(c);
    return changed;
}

static int ir_is_const_vreg_(const ir_const_i64_t* c, ir_vreg_t v, i64_t value)
{
    return ir_vreg_valid(v) && c[v.id].known && c[v.id].value == value;
}

static void ir_replace_with_mov_(ir_instr_t* in, ir_vreg_t src)
{
    in->op  = IR_OP_MOV;
    in->a   = src;
    in->b   = ir_no_vreg();
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
                if (ir_vreg_valid(in->dst))
                {
                    c[in->dst.id].known = 1;
                    c[in->dst.id].value = in->imm;
                }
                break;

            case IR_OP_MOV:
                if (ir_vreg_valid(in->dst))
                {
                    if (ir_vreg_valid(in->a) && c[in->a.id].known)
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

                if (ir_vreg_valid(in->dst))
                    c[in->dst.id].known = 0;
                break;

            case IR_OP_SUB_I64:
                if (ir_is_const_vreg_(c, in->b, 0))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }

                if (ir_vreg_valid(in->dst))
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
                    in->a   = ir_no_vreg();
                    in->b   = ir_no_vreg();

                    changed = 1;
                }

                if (ir_vreg_valid(in->dst))
                    c[in->dst.id].known = 0;
                break;

            case IR_OP_DIV_I64:
                if (ir_is_const_vreg_(c, in->b, 1))
                {
                    ir_replace_with_mov_(in, in->a);
                    changed = 1;
                }

                if (ir_vreg_valid(in->dst))
                    c[in->dst.id].known = 0;
                break;

            default:
                if (ir_instr_has_dst(in))
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

    if (!ir_vreg_valid(v))
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

        if (ir_op_is_pure(in->op) &&
            ir_instr_has_dst(in) &&
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
            ir_vreg_valid(in->dst) &&
            ir_vreg_valid(in->a) &&
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


