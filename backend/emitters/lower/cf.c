#include "backend/emitters/runtime/runtime.h"
#include "backend/emitters/lower/lower.h"

#define NASM_TRY(x)  do { err_t rc__ = (x); if (rc__ != OK) return rc__; } while (0)
#define NASM_EMIT(l, ...) ir_emit((l)->f, (ir_instr_t){ __VA_ARGS__ })

err_t nasm_lower_loop_push(nasm_lower_t* l, size_t label)
{
    BE_VEC_GROW(l->loop_end_labels, l->loop_cap, l->loop_amount + 1, size_t);
    l->loop_end_labels[l->loop_amount++] = label;
    return OK;
}

void nasm_lower_loop_pop(nasm_lower_t* l)
{
    if (l && l->loop_amount > 0)
        l->loop_amount--;
}

err_t nasm_lower_cond_false(nasm_lower_t* l, const ast_node_t* cond, size_t false_label)
{
    if (cond && cond->kind == ASTK_BINARY && cond->u.binary.op == TOK_OP_AND)
    {
        const ast_node_t* lhs = cond->left;
        const ast_node_t* rhs = lhs ? lhs->right : NULL;

        BE_CHECK(l->be, lhs && rhs, cond, "Bad && condition");
        NASM_TRY(nasm_lower_cond_false(l, lhs, false_label));
        return nasm_lower_cond_false(l, rhs, false_label);
    }

    if (cond && cond->kind == ASTK_BINARY && nasm_is_cmp_op(cond->u.binary.op))
    {
        const ast_node_t* lhs = cond->left;
        const ast_node_t* rhs = lhs ? lhs->right : NULL;
        ir_vreg_t         a   = { 0 };
        ir_vreg_t         b   = { 0 };

        BE_CHECK(l->be, lhs && rhs, cond, "Bad comparison condition");

        if (nasm_infer_expr_type(l->be, lhs) == AST_TYPE_FLOAT ||
            nasm_infer_expr_type(l->be, rhs) == AST_TYPE_FLOAT)
        {
            NASM_TRY(nasm_lower_expr_f64(l, lhs, &a));
            NASM_TRY(nasm_lower_expr_f64(l, rhs, &b));

            return NASM_EMIT(l,
                .op       = IR_OP_JCC_FALSE_F64,
                .a        = a,
                .b        = b,
                .imm      = cond->u.binary.op,
                .label_id = false_label,
                .pos      = cond->pos);
        }

        NASM_TRY(nasm_lower_expr_i64(l, lhs, &a));
        NASM_TRY(nasm_lower_expr_i64(l, rhs, &b));

        return NASM_EMIT(l,
            .op       = IR_OP_JCC_FALSE_I64,
            .a        = a,
            .b        = b,
            .imm      = cond->u.binary.op,
            .label_id = false_label,
            .pos      = cond->pos);
    }

    ir_vreg_t c = { 0 };
    NASM_TRY(nasm_lower_expr_i64(l, cond, &c));

    return NASM_EMIT(l,
        .op       = IR_OP_JZ,
        .a        = c,
        .label_id = false_label,
        .pos      = cond->pos);
}

err_t nasm_lower_jz(nasm_lower_t* l, const ast_node_t* cond, size_t false_label)
{
    return nasm_lower_cond_false(l, cond, false_label);
}

err_t nasm_lower_while(nasm_lower_t* l, const ast_node_t* w)
{
    const ast_node_t* cond    = w->left;
    const ast_node_t* body    = cond ? cond->right : NULL;
    size_t            L_begin = ir_new_label(l->f);
    size_t            L_end   = ir_new_label(l->f);

    BE_CHECK(l->be, cond && body, w, "Bad WHILE node");

    NASM_TRY(NASM_EMIT(l, .op = IR_OP_LABEL, .label_id = L_begin, .pos = w->pos));
    NASM_TRY(nasm_lower_jz(l, cond, L_end));
    NASM_TRY(nasm_lower_loop_push(l, L_end));

    err_t rc = nasm_lower_stmt(l, body);
    nasm_lower_loop_pop(l);
    if (rc != OK) return rc;

    NASM_TRY(NASM_EMIT(l, .op = IR_OP_JMP, .label_id = L_begin, .pos = w->pos));
    return NASM_EMIT(l, .op = IR_OP_LABEL, .label_id = L_end, .pos = w->pos);
}

err_t nasm_lower_break(nasm_lower_t* l, const ast_node_t* brk)
{
    BE_CHECK(l->be, l->loop_amount > 0, brk, "break outside loop");

    return NASM_EMIT(l,
        .op       = IR_OP_JMP,
        .label_id = l->loop_end_labels[l->loop_amount - 1],
        .pos      = brk->pos);
}

err_t nasm_lower_if(nasm_lower_t* l, const ast_node_t* ifn)
{
    const ast_node_t* cond    = ifn->left;
    const ast_node_t* then_st = cond ? cond->right : NULL;
    const ast_node_t* tail    = then_st ? then_st->right : NULL;
    size_t            L_end;
    size_t            L_next;

    BE_CHECK(l->be, cond && then_st, ifn, "Bad IF node");

    L_end  = ir_new_label(l->f);
    L_next = tail ? ir_new_label(l->f) : L_end;

    NASM_TRY(nasm_lower_jz(l, cond, L_next));
    NASM_TRY(nasm_lower_stmt(l, then_st));

    if (tail)
    {
        NASM_TRY(NASM_EMIT(l, .op = IR_OP_JMP,   .label_id = L_end,  .pos = ifn->pos));
        NASM_TRY(NASM_EMIT(l, .op = IR_OP_LABEL, .label_id = L_next, .pos = tail->pos));
        NASM_TRY(nasm_lower_if_tail(l, tail, L_end));
    }

    return NASM_EMIT(l, .op = IR_OP_LABEL, .label_id = L_end, .pos = ifn->pos);
}

err_t nasm_lower_if_tail(nasm_lower_t* l, const ast_node_t* tail, size_t L_end)
{
    if (!tail)
        return OK;

    if (tail->kind == ASTK_ELSE)
    {
        const ast_node_t* body = tail->left;

        BE_CHECK(l->be, body != NULL, tail, "Bad ELSE node");
        return nasm_lower_stmt(l, body);
    }

    if (tail->kind == ASTK_BRANCH)
    {
        const ast_node_t* cond   = tail->left;
        const ast_node_t* body   = cond ? cond->right : NULL;
        const ast_node_t* next   = body ? body->right : NULL;
        size_t            L_next = next ? ir_new_label(l->f) : L_end;

        BE_CHECK(l->be, cond && body, tail, "Bad ELIF branch");

        NASM_TRY(nasm_lower_jz(l, cond, L_next));
        NASM_TRY(nasm_lower_stmt(l, body));

        if (!next)
            return OK;

        NASM_TRY(NASM_EMIT(l, .op = IR_OP_JMP,   .label_id = L_end,  .pos = tail->pos));
        NASM_TRY(NASM_EMIT(l, .op = IR_OP_LABEL, .label_id = L_next, .pos = next->pos));
        return nasm_lower_if_tail(l, next, L_end);
    }

    BE_FAIL_NODE(l->be, tail, "Bad IF tail child %s", ast_kind_to_cstr(tail->kind));
}
