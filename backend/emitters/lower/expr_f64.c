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

#define EMIT_F64_IMM(dst_, imm_, pos_)                  \
    IR_EMIT(.op = IR_OP_MOV_IMM_F64,                    \
            .dst = (dst_), .imm = ir_f64_to_bits(imm_), \
            .pos = (pos_))

#define EMIT_I64_TO_F64(dst_, src_, pos_)               \
    IR_EMIT(.op = IR_OP_I64_TO_F64,                     \
            .dst = (dst_), .a = (src_), .pos = (pos_))

#define F64_FROM_I64(expr_, pos_)                       \
    do                                                  \
    {                                                   \
        ir_vreg_t iv = { 0 };                           \
        err_t rc = nasm_lower_expr_i64(l, expr_, &iv);  \
        if (rc != OK) return rc;                        \
                                                        \
        ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);  \
        rc = EMIT_I64_TO_F64(fv, iv, pos_);             \
        if (rc != OK) return rc;                        \
                                                        \
        OUT(fv);                                        \
    } while (0)

ir_op_t binop_to_ir_f64(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_PLUS:  return IR_OP_ADD_F64;
        case TOK_OP_MINUS: return IR_OP_SUB_F64;
        case TOK_OP_MUL:   return IR_OP_MUL_F64;
        case TOK_OP_DIV:   return IR_OP_DIV_F64;
        case TOK_OP_POW:   return IR_OP_POW_F64;

        case TOK_OP_EQ:    return IR_OP_CMP_EQ_F64;
        case TOK_OP_NEQ:   return IR_OP_CMP_NE_F64;
        case TOK_OP_LT:    return IR_OP_CMP_LT_F64;
        case TOK_OP_GT:    return IR_OP_CMP_GT_F64;
        case TOK_OP_LTE:   return IR_OP_CMP_LE_F64;
        case TOK_OP_GTE:   return IR_OP_CMP_GE_F64;

        default:           return IR_OP_NOP;
    }
}

size_t nasm_ast_arg_count(const ast_node_t* args)
{
    size_t n = 0;

    if (!args)
        return 0;

    for (const ast_node_t* a = args->left; a; a = a->right)
        n++;

    return n;
}

err_t nasm_lower_builtin_f64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    const ast_node_t* args = call->left;
    size_t argc = nasm_ast_arg_count(args);
    err_t rc = OK;

    if (be_streq(name, "fin") || be_streq(name, "nocap"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

        RC_CH(IR_EMIT(.op = IR_OP_SCANF_F64, .dst = dst, .pos = call->pos));
        OUT(dst);
    }

    if (be_streq(name, "fout") || be_streq(name, "rizz"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        ir_vreg_t v = { 0 };

        RC_CH(nasm_lower_expr_f64(l, nasm_ast_arg_at(args, 0), &v));
        RC_CH(IR_EMIT(.op = IR_OP_PRINTF_F64, .a = v, .pos = call->pos));
        OUT(v);
    }

    if (be_streq(name, "sqrt"))
    {
        BE_CHECK(l->be, argc == 1, call, "sqrt(x) takes 1 arg");

        ir_vreg_t x = { 0 };
        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

        RC_CH(nasm_lower_expr_f64(l, nasm_ast_arg_at(args, 0), &x));
        RC_CH(IR_EMIT(.op = IR_OP_SQRT_F64, .dst = dst, .a = x, .pos = call->pos));
        OUT(dst);
    }

    if (be_streq(name, "pow") || be_streq(name, "mpow"))
    {
        BE_CHECK(l->be, argc == 2, call, "%s(x,y) takes 2 args", name);

        const ast_node_t* base = nasm_ast_arg_at(args, 0);
        const ast_node_t* exp  = nasm_ast_arg_at(args, 1);
        i64_t exp_i = 0;

        if (nasm_ast_i64_literal(exp, &exp_i) && exp_i >= 0 && exp_i <= 4)
        {
            if (exp_i == 0)
            {
                ir_vreg_t one = ir_new_vreg(l->f, IR_TYPE_F64);

                RC_CH(EMIT_F64_IMM(one, 1.0, call->pos));
                OUT(one);
            }

            ir_vreg_t x = { 0 };

            RC_CH(nasm_lower_expr_f64(l, base, &x));

            if (exp_i == 1)
                OUT(x);

            ir_vreg_t cur = x;

            for (i64_t i = 1; i < exp_i; ++i)
            {
                ir_vreg_t next = ir_new_vreg(l->f, IR_TYPE_F64);

                RC_CH(IR_EMIT(
                    .op  = IR_OP_MUL_F64,
                    .dst = next,
                    .a   = cur,
                    .b   = x,
                    .pos = call->pos,
                ));

                cur = next;
            }

            OUT(cur);
        }

        ir_vreg_t x = { 0 };
        ir_vreg_t y = { 0 };
        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

        RC_CH(nasm_lower_expr_f64(l, base, &x));
        RC_CH(nasm_lower_expr_f64(l, exp, &y));
        RC_CH(IR_EMIT(
            .op  = IR_OP_POW_F64,
            .dst = dst,
            .a   = x,
            .b   = y,
            .pos = call->pos,
        ));

        OUT(dst);
    }

    ir_vreg_t iv = { 0 };
    ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);

    RC_CH(nasm_lower_builtin_i64(l, call, &iv));
    RC_CH(EMIT_I64_TO_F64(fv, iv, call->pos));
    OUT(fv);
}

err_t nasm_lower_expr_f64(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    BE_CHECK(l->be, e != NULL, e, "Null expression");

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
        {
            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);
            double val = e->u.num.lit_type == LIT_FLOAT
                       ? e->u.num.lit.f64
                       : (double)e->u.num.lit.i64;
            err_t rc = EMIT_F64_IMM(dst, val, e->pos);

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
                ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);
                err_t rc = IR_EMIT(
                    .op   = IR_OP_LOAD_SLOT,
                    .dst  = dst,
                    .slot = b->offset,
                    .pos  = e->pos,
                );

                if (rc != OK)
                    return rc;

                OUT(dst);
            }

            F64_FROM_I64(e, e->pos);
        }

        case ASTK_UNARY:
        {
            if (e->u.unary.op == TOK_OP_PLUS)
                return nasm_lower_expr_f64(l, e->left, out);

            if (e->u.unary.op == TOK_OP_MINUS)
            {
                ir_vreg_t a = { 0 };
                ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);
                err_t rc = OK;

                RC_CH(nasm_lower_expr_f64(l, e->left, &a));
                RC_CH(IR_EMIT(.op = IR_OP_NEG_F64, .dst = dst, .a = a, .pos = e->pos));
                OUT(dst);
            }

            BE_FAIL_NODE(l->be, e, "Unsupported float unary operator");
        }

        case ASTK_BINARY:
        {
            const ast_node_t* lhs = e->left;
            const ast_node_t* rhs = lhs ? lhs->right : NULL;

            BE_CHECK(l->be, lhs && rhs, e, "Bad binary node");

            ir_op_t op = binop_to_ir_f64(e->u.binary.op);

            BE_CHECK(l->be, op != IR_OP_NOP, e, "Unsupported float binary op");

            ir_vreg_t a = { 0 };
            ir_vreg_t b = { 0 };
            ir_type_t dst_type = nasm_is_bool_op(e->u.binary.op) ? IR_TYPE_I64 : IR_TYPE_F64;
            ir_vreg_t dst = ir_new_vreg(l->f, dst_type);
            err_t rc = OK;

            RC_CH(nasm_lower_expr_f64(l, lhs, &a));
            RC_CH(nasm_lower_expr_f64(l, rhs, &b));
            RC_CH(IR_EMIT(.op = op, .dst = dst, .a = a, .b = b, .pos = e->pos));

            if (dst_type == IR_TYPE_F64)
                OUT(dst);

            ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);

            RC_CH(EMIT_I64_TO_F64(fv, dst, e->pos));
            OUT(fv);
        }

        case ASTK_CALL:
            return nasm_lower_call_expr_f64(l, e, out);

        case ASTK_BUILTIN_UNARY:
        {
            const ast_node_t* arg = e->left;

            if (e->u.builtin_unary.id == AST_BUILTIN_ITOF)
                F64_FROM_I64(arg, e->pos);

            ir_vreg_t fv = { 0 };
            err_t rc = OK;

            RC_CH(nasm_lower_expr_f64(l, arg, &fv));

            if (e->u.builtin_unary.id == AST_BUILTIN_FTOI)
            {
                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);
                ir_vreg_t out_f = ir_new_vreg(l->f, IR_TYPE_F64);

                RC_CH(IR_EMIT(.op = IR_OP_F64_TO_I64, .dst = iv, .a = fv, .pos = e->pos));
                RC_CH(EMIT_I64_TO_F64(out_f, iv, e->pos));
                OUT(out_f);
            }

            ir_op_t op = IR_OP_NOP;

            switch (e->u.builtin_unary.id)
            {
                case AST_BUILTIN_FLOOR: op = IR_OP_FLOOR_F64; break;
                case AST_BUILTIN_CEIL:  op = IR_OP_CEIL_F64;  break;
                case AST_BUILTIN_ROUND: op = IR_OP_ROUND_F64; break;

                default:
                    BE_FAIL_NODE(l->be, e, "Unsupported float builtin");
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

            RC_CH(IR_EMIT(.op = op, .dst = dst, .a = fv, .pos = e->pos));
            OUT(dst);
        }

        default:
            F64_FROM_I64(e, e->pos);
    }
}

#undef IR_EMIT
#undef RC_CH
#undef OUT
#undef EMIT_F64_IMM
#undef EMIT_I64_TO_F64
#undef F64_FROM_I64
