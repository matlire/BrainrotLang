#include "backend/emitters/lower/lower.h"

#define IR_EMIT(...) ir_emit(l->f, (ir_instr_t){ __VA_ARGS__ })

#define RC_CH(expr)     \
    do                  \
    {                   \
        rc = (expr);    \
        if (rc != OK)   \
            return rc;  \
    } while (0)

#define OUT(v)          \
    do                  \
    {                   \
        *out = (v);     \
        return OK;      \
    } while (0)

#define EMIT_I64_IMM(dst_, imm_, pos_)                  \
    IR_EMIT(.op = IR_OP_MOV_IMM_I64,                    \
            .dst = (dst_), .imm = (imm_), .pos = (pos_))

#define EMIT_F64_IMM(dst_, imm_, pos_)                  \
    IR_EMIT(.op = IR_OP_MOV_IMM_F64,                    \
            .dst = (dst_), .imm = ir_f64_to_bits(imm_), \
            .pos = (pos_))

#define EMIT_F64_TO_I64(dst_, src_, pos_)               \
    IR_EMIT(.op = IR_OP_F64_TO_I64,                     \
            .dst = (dst_), .a = (src_), .pos = (pos_))

#define EMIT_LOAD_SLOT(dst_, slot_, pos_)               \
    IR_EMIT(.op = IR_OP_LOAD_SLOT,                      \
            .dst = (dst_), .slot = (slot_), .pos = (pos_))

#define LOWER_F64_EXPR_TO_I64(expr_, pos_)              \
    do                                                  \
    {                                                   \
        ir_vreg_t fv = { 0 };                           \
        err_t rc = nasm_lower_expr_f64(l, expr_, &fv);  \
        if (rc != OK) return rc;                        \
                                                        \
        ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);  \
        rc = EMIT_F64_TO_I64(iv, fv, pos_);             \
        if (rc != OK) return rc;                        \
                                                        \
        OUT(iv);                                        \
    } while (0)

err_t nasm_lower_unary_i64(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    ir_vreg_t a = { 0 };

    err_t rc = nasm_lower_expr_i64(l, e->left, &a);
    if (rc != OK)
        return rc;

    if (e->u.unary.op == TOK_OP_PLUS)
        OUT(a);

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
    ir_op_t op = IR_OP_NOP;

    switch (e->u.unary.op)
    {
        case TOK_OP_MINUS: op = IR_OP_NEG_I64; break;
        case TOK_OP_NOT:   op = IR_OP_NOT_I64; break;

        default:
            BE_FAIL_NODE(l->be, e, "Unsupported unary operator");
    }

    rc = IR_EMIT(
        .op  = op,
        .dst = dst,
        .a   = a,
        .pos = e->pos,
    );
    if (rc != OK)
        return rc;

    OUT(dst);
}

err_t nasm_lower_binary_i64(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad binary node");

    if (e->u.binary.op == TOK_OP_AND)
        return nasm_lower_logical_and_i64(l, e, out);

    if (e->u.binary.op == TOK_OP_OR)
        return nasm_lower_logical_or_i64(l, e, out);

    if (e->u.binary.op == TOK_OP_POW)
    {
        i64_t exp = 0;

        if (nasm_ast_i64_literal(rhs, &exp) && exp >= 0 && exp <= 4)
            return nasm_lower_i64_pow_small_const(l, lhs, exp, e->pos, out);
    }

    ast_type_t lt = nasm_infer_expr_type(l->be, lhs);
    ast_type_t rt = nasm_infer_expr_type(l->be, rhs);

    if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT)
        return nasm_is_bool_op(e->u.binary.op)
             ? nasm_lower_float_cmp_i64(l, e, out)
             : nasm_lower_expr_f64_to_i64(l, e, out);

    ir_op_t op = binop_to_ir(e->u.binary.op);

    BE_CHECK(l->be, op != IR_OP_NOP, e,
             "Integer binary operator not supported yet");

    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };

    err_t rc = nasm_lower_expr_i64(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = nasm_lower_expr_i64(l, rhs, &b);
    if (rc != OK)
        return rc;

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    rc = IR_EMIT(
        .op  = op,
        .dst = dst,
        .a   = a,
        .b   = b,
        .pos = e->pos,
    );
    if (rc != OK)
        return rc;

    OUT(dst);
}

err_t nasm_lower_expr_f64_to_i64(nasm_lower_t*     l,
                                 const ast_node_t* e,
                                 ir_vreg_t*        out)
{
    LOWER_F64_EXPR_TO_I64(e, e->pos);
}

err_t nasm_lower_i64_pow_small_const(nasm_lower_t*     l,
                                     const ast_node_t* base,
                                     i64_t             exp_i,
                                     token_pos_t       pos,
                                     ir_vreg_t*        out)
{
    if (exp_i < 0 || exp_i > 4)
        return ERR_BAD_ARG;

    if (exp_i == 0)
    {
        ir_vreg_t one = ir_new_vreg(l->f, IR_TYPE_I64);

        err_t rc = EMIT_I64_IMM(one, 1, pos);
        if (rc != OK)
            return rc;

        OUT(one);
    }

    ir_vreg_t x = { 0 };

    err_t rc = nasm_lower_expr_i64(l, base, &x);
    if (rc != OK)
        return rc;

    if (exp_i == 1)
        OUT(x);

    ir_vreg_t cur = x;

    for (i64_t i = 1; i < exp_i; ++i)
    {
        ir_vreg_t next = ir_new_vreg(l->f, IR_TYPE_I64);

        rc = IR_EMIT(
            .op  = IR_OP_MUL_I64,
            .dst = next,
            .a   = cur,
            .b   = x,
            .pos = pos,
        );
        if (rc != OK)
            return rc;

        cur = next;
    }

    OUT(cur);
}

err_t nasm_lower_logical_and_i64(nasm_lower_t*     l,
                                 const ast_node_t* e,
                                 ir_vreg_t*        out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad && node");

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
    size_t L_false = ir_new_label(l->f);
    size_t L_end   = ir_new_label(l->f);
    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };
    err_t rc = OK;

    RC_CH(nasm_lower_expr_i64(l, lhs, &a));
    RC_CH(IR_EMIT(.op = IR_OP_JZ, .a = a, .label_id = L_false, .pos = lhs->pos));
    RC_CH(nasm_lower_expr_i64(l, rhs, &b));
    RC_CH(IR_EMIT(.op = IR_OP_JZ, .a = b, .label_id = L_false, .pos = rhs->pos));
    RC_CH(EMIT_I64_IMM(dst, 1, e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_JMP, .label_id = L_end, .pos = e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_LABEL, .label_id = L_false, .pos = e->pos));
    RC_CH(EMIT_I64_IMM(dst, 0, e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_LABEL, .label_id = L_end, .pos = e->pos));

    OUT(dst);
}

err_t nasm_lower_logical_or_i64(nasm_lower_t*     l,
                                const ast_node_t* e,
                                ir_vreg_t*        out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad || node");

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
    size_t L_rhs   = ir_new_label(l->f);
    size_t L_false = ir_new_label(l->f);
    size_t L_end   = ir_new_label(l->f);
    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };
    err_t rc = OK;

    RC_CH(nasm_lower_expr_i64(l, lhs, &a));
    RC_CH(IR_EMIT(.op = IR_OP_JZ, .a = a, .label_id = L_rhs, .pos = lhs->pos));
    RC_CH(EMIT_I64_IMM(dst, 1, e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_JMP, .label_id = L_end, .pos = e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_LABEL, .label_id = L_rhs, .pos = rhs->pos));
    RC_CH(nasm_lower_expr_i64(l, rhs, &b));
    RC_CH(IR_EMIT(.op = IR_OP_JZ, .a = b, .label_id = L_false, .pos = rhs->pos));
    RC_CH(EMIT_I64_IMM(dst, 1, e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_JMP, .label_id = L_end, .pos = e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_LABEL, .label_id = L_false, .pos = e->pos));
    RC_CH(EMIT_I64_IMM(dst, 0, e->pos));
    RC_CH(IR_EMIT(.op = IR_OP_LABEL, .label_id = L_end, .pos = e->pos));

    OUT(dst);
}

err_t nasm_lower_float_cmp_i64(nasm_lower_t*     l,
                               const ast_node_t* e,
                               ir_vreg_t*        out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad float comparison node");

    ir_op_t op = binop_to_ir_f64(e->u.binary.op);

    BE_CHECK(l->be, op != IR_OP_NOP, e,
             "Unsupported float comparison operator");

    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };

    err_t rc = nasm_lower_expr_f64(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = nasm_lower_expr_f64(l, rhs, &b);
    if (rc != OK)
        return rc;

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    rc = IR_EMIT(
        .op  = op,
        .dst = dst,
        .a   = a,
        .b   = b,
        .pos = e->pos,
    );
    if (rc != OK)
        return rc;

    OUT(dst);
}

int nasm_is_cmp_op(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_EQ:
        case TOK_OP_NEQ:
        case TOK_OP_LT:
        case TOK_OP_GT:
        case TOK_OP_LTE:
        case TOK_OP_GTE:
            return 1;

        default:
            return 0;
    }
}

int nasm_is_bool_op(token_kind_t op)
{
    return nasm_is_cmp_op(op) || op == TOK_OP_AND || op == TOK_OP_OR;
}

err_t nasm_lower_expr_i64(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    BE_CHECK(l->be, e != NULL, e, "Null expression");

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
        {
            if (e->u.num.lit_type == LIT_FLOAT)
            {
                ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

                err_t rc = EMIT_F64_IMM(fv, e->u.num.lit.f64, e->pos);
                if (rc != OK)
                    return rc;

                rc = EMIT_F64_TO_I64(iv, fv, e->pos);
                if (rc != OK)
                    return rc;

                OUT(iv);
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

            err_t rc = EMIT_I64_IMM(dst, e->u.num.lit.i64, e->pos);
            if (rc != OK)
                return rc;

            OUT(dst);
        }

        case ASTK_IDENT:
        {
            ssize_t bi = be_bind_lookup(l->be, e->u.ident.name_id);
            BE_CHECK(l->be, bi >= 0, e, "Unknown variable");

            const binding_t* b = &l->be->binds[(size_t)bi];

            if (b->type == AST_TYPE_FLOAT)
            {
                ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

                err_t rc = EMIT_LOAD_SLOT(fv, b->offset, e->pos);
                if (rc != OK)
                    return rc;

                rc = EMIT_F64_TO_I64(iv, fv, e->pos);
                if (rc != OK)
                    return rc;

                OUT(iv);
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

            err_t rc = EMIT_LOAD_SLOT(dst, b->offset, e->pos);
            if (rc != OK)
                return rc;

            OUT(dst);
        }

        case ASTK_UNARY:
            return nasm_lower_unary_i64(l, e, out);

        case ASTK_BINARY:
            return nasm_lower_binary_i64(l, e, out);

        case ASTK_CALL:
            return nasm_lower_call_expr_i64(l, e, out);

        case ASTK_BUILTIN_UNARY:
            LOWER_F64_EXPR_TO_I64(e->u.builtin_unary.id == AST_BUILTIN_FTOI
                                  ? e->left
                                  : e,
                                  e->pos);

        default:
            BE_FAIL_NODE(l->be, e, "IR unsupported expression %s",
                         ast_kind_to_cstr(e->kind));
    }
}


#undef IR_EMIT
#undef RC_CH
#undef OUT
#undef EMIT_I64_IMM
#undef EMIT_F64_IMM
#undef EMIT_F64_TO_I64
#undef EMIT_LOAD_SLOT
#undef LOWER_F64_EXPR_TO_I64
