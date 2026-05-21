#include "backend/emitters/lower/lower.h"

#define IR_EMIT(...) ir_emit(out, (ir_instr_t){ __VA_ARGS__ })

#define RC_CH(expr)         \
    do                      \
    {                       \
        rc = (expr);        \
        if (rc != OK)       \
            goto cleanup;   \
    } while (0)

#define EMIT_DEFAULT_RET()                                                  \
    do                                                                      \
    {                                                                       \
        if (meta->ret_type == AST_TYPE_VOID)                                \
            RC_CH(IR_EMIT(.op = IR_OP_RET));                                \
        else                                                                \
        {                                                                   \
            int f64 = meta->ret_type == AST_TYPE_FLOAT;                     \
            ir_vreg_t z = ir_new_vreg(out, f64 ? IR_TYPE_F64 : IR_TYPE_I64);\
                                                                            \
            RC_CH(IR_EMIT(                                                  \
                .op  = f64 ? IR_OP_MOV_IMM_F64 : IR_OP_MOV_IMM_I64,         \
                .dst = z,                                                   \
                .imm = f64 ? ir_f64_to_bits(0.0) : 0,                       \
            ));                                                             \
                                                                            \
            RC_CH(IR_EMIT(.op = IR_OP_RET, .a = z));                        \
        }                                                                   \
    } while (0)

int nasm_ast_i64_literal(const ast_node_t* n, i64_t* out)
{
    int ok = n && n->kind == ASTK_NUM_LIT && n->u.num.lit_type == LIT_INT;

    if (ok && out)
        *out = n->u.num.lit.i64;

    return ok;
}

static int ast_func_body_ends_with_return_(const ast_node_t* body)
{
    if (!body || body->kind != ASTK_BLOCK || !body->left)
        return 0;

    const ast_node_t* last = body->left;

    while (last->right)
        last = last->right;

    return last->kind == ASTK_RETURN;
}

err_t nasm_lower_func(backend_t* be, const ast_node_t* fn, ir_func_t* out)
{
    const func_meta_t* meta = be_find_func(be, fn->u.func.name_id);

    BE_CHECK(be, meta != NULL, fn, "Internal: missing function metadata");

    err_t rc = ir_func_ctor(out, meta->name_id, meta->label, meta->ret_type);
    if (rc != OK)
        return rc;

    be->cur_fn            = meta;
    be->bind_amount       = 0;
    be->scope_depth       = 1;
    be->next_local_offset = 0;

    const ast_node_t* plist = fn->left;
    const ast_node_t* body  = plist ? plist->right : NULL;

    BE_CHECK(be, plist && plist->kind == ASTK_PARAM_LIST,
             fn, "FUNC missing PARAM_LIST");
    BE_CHECK(be, body != NULL, fn, "FUNC missing body");

    nasm_lower_t l = {
        .be        = be,
        .f         = out,
        .meta      = meta,
        .ret_label = ir_new_label(out),
    };

    for (const ast_node_t* p = plist->left; p; p = p->right)
    {
        BE_CHECK(be,
                 p->u.param.type == AST_TYPE_INT ||
                 p->u.param.type == AST_TYPE_PTR ||
                 p->u.param.type == AST_TYPE_FLOAT,
                 p, "NASM supports only int/ptr/float params");

        size_t slot = 0;

        RC_CH(ir_add_slot(out, p->u.param.name_id, p->u.param.type, &slot));
        RC_CH(be_bind_push(be, p->u.param.name_id, p->u.param.type,  slot, be->scope_depth));
    }

    RC_CH(nasm_lower_stmt(&l, body));

    if (meta->ret_type == AST_TYPE_VOID || !ast_func_body_ends_with_return_(body))
    {
        RC_CH(IR_EMIT(
            .op       = IR_OP_LABEL,
            .label_id = l.ret_label,
        ));

        EMIT_DEFAULT_RET();
    }

    out->frame_slots = out->slot_count;

cleanup:
    free(l.loop_end_labels);
    return rc;
}

ast_type_t nasm_infer_expr_type(backend_t* be, const ast_node_t* e)
{
    if (!be || !e)
        return AST_TYPE_UNKNOWN;

    if (e->type != AST_TYPE_UNKNOWN)
        return e->type;

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
            return (e->u.num.lit_type == LIT_FLOAT) ? AST_TYPE_FLOAT : AST_TYPE_INT;

        case ASTK_IDENT:
        {
            ssize_t idx = be_bind_lookup(be, e->u.ident.name_id);
            return (idx >= 0) ? be->binds[(size_t)idx].type : AST_TYPE_UNKNOWN;
        }

        case ASTK_CALL:
        {
            const char* name = ast_name_cstr(be->tree, e->u.call.name_id);
            if (!name)
                return AST_TYPE_UNKNOWN;

            if (be_streq(name, "in")  || be_streq(name, "cap") ||
                be_streq(name, "cin") || be_streq(name, "stinky") ||
                be_streq(name, "out") || be_streq(name, "pookie") ||
                be_streq(name, "cout") || be_streq(name, "menace"))
                return AST_TYPE_INT;

            if (be_streq(name, "fin")  || be_streq(name, "nocap") ||
                be_streq(name, "fout") || be_streq(name, "rizz")  ||
                be_streq(name, "sqrt") || be_streq(name, "pow")   ||
                be_streq(name, "mpow"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "draw")     || be_streq(name, "gyat") ||
                be_streq(name, "clean_vm") || be_streq(name, "skibidi") ||
                be_streq(name, "set_pixel"))
                return AST_TYPE_VOID;

            const func_meta_t* fm = be_find_func(be, e->u.call.name_id);
            return fm ? fm->ret_type : AST_TYPE_UNKNOWN;
        }

        case ASTK_BUILTIN_UNARY:
            return (e->u.builtin_unary.id == AST_BUILTIN_FTOI)
                 ? AST_TYPE_INT
                 : AST_TYPE_FLOAT;

        case ASTK_UNARY:
            if (e->u.unary.op == TOK_OP_NOT)
                return AST_TYPE_INT;

            return nasm_infer_expr_type(be, e->left);

        case ASTK_BINARY:
        {
            if (nasm_is_bool_op(e->u.binary.op))
                return AST_TYPE_INT;

            const ast_node_t* lhs = e->left;
            const ast_node_t* rhs = lhs ? lhs->right : NULL;

            ast_type_t lt = nasm_infer_expr_type(be, lhs);
            ast_type_t rt = nasm_infer_expr_type(be, rhs);

            if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT)
                return AST_TYPE_FLOAT;

            if (lt == AST_TYPE_UNKNOWN || rt == AST_TYPE_UNKNOWN)
                return AST_TYPE_UNKNOWN;

            return AST_TYPE_INT;
        }

        default:
            return AST_TYPE_UNKNOWN;
    }
}

#undef IR_EMIT
#undef RC_CH
#undef EMIT_DEFAULT_RET
