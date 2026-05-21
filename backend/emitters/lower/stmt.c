#include "backend/emitters/lower/lower.h"

err_t nasm_lower_expr_stmt(nasm_lower_t* l, const ast_node_t* st)
{
    ir_vreg_t ignored = { 0 };

    return nasm_infer_expr_type(l->be, st->left) == AST_TYPE_FLOAT
         ? nasm_lower_expr_f64(l, st->left, &ignored)
         : nasm_lower_expr_i64(l, st->left, &ignored);
}

static int nasm_match_self_add_imm_(const ast_node_t* asn,
                                    size_t            name_id,
                                    i64_t*            out_imm)
{
    if (!asn || asn->kind != ASTK_ASSIGN || !asn->left)
        return 0;

    const ast_node_t* e = asn->left;

    if (e->kind != ASTK_BINARY)
        return 0;

    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    if (!lhs || !rhs)
        return 0;

    if (e->u.binary.op == TOK_OP_PLUS)
    {
        if (lhs->kind == ASTK_IDENT &&
            lhs->u.ident.name_id == name_id &&
            rhs->kind == ASTK_NUM_LIT &&
            rhs->u.num.lit_type == LIT_INT)
        {
            *out_imm = rhs->u.num.lit.i64;
            return 1;
        }

        if (rhs->kind == ASTK_IDENT &&
            rhs->u.ident.name_id == name_id &&
            lhs->kind == ASTK_NUM_LIT &&
            lhs->u.num.lit_type == LIT_INT)
        {
            *out_imm = lhs->u.num.lit.i64;
            return 1;
        }
    }

    if (e->u.binary.op == TOK_OP_MINUS &&
        lhs->kind == ASTK_IDENT &&
        lhs->u.ident.name_id == name_id &&
        rhs->kind == ASTK_NUM_LIT &&
        rhs->u.num.lit_type == LIT_INT)
    {
        *out_imm = -rhs->u.num.lit.i64;
        return 1;
    }

    return 0;
}

err_t nasm_lower_stmt(nasm_lower_t* l, const ast_node_t* st)
{
    if (!st)
        return OK;

    switch (st->kind)
    {
        case ASTK_BLOCK:     return nasm_lower_block(l, st);
        case ASTK_VAR_DECL:  return nasm_lower_vdecl(l, st);
        case ASTK_ASSIGN:    return nasm_lower_assign(l, st);
        case ASTK_RETURN:    return nasm_lower_return(l, st);
        case ASTK_EXPR_STMT: return nasm_lower_expr_stmt(l, st);
        case ASTK_CALL_STMT: return nasm_lower_call_stmt(l, st);
        case ASTK_IF:        return nasm_lower_if(l, st);
        case ASTK_WHILE:     return nasm_lower_while(l, st);
        case ASTK_BREAK:     return nasm_lower_break(l, st);

        case ASTK_COUT:
        case ASTK_ICOUT:     return nasm_lower_print_i64(l, st);
        case ASTK_FCOUT:     return nasm_lower_print_f64(l, st);

        default:
            BE_FAIL_NODE(l->be, st, "NASM IR: unsupported statement %s",
                         ast_kind_to_cstr(st->kind));
    }
}

err_t nasm_lower_block(nasm_lower_t* l, const ast_node_t* block)
{
    err_t rc = OK;

    l->be->scope_depth++;
    size_t depth = l->be->scope_depth;

    for (const ast_node_t* c = block->left; c; c = c->right)
    {
        rc = nasm_lower_stmt(l, c);
        if (rc != OK)
            break;
    }

    be_bind_pop_depth(l->be, depth);
    l->be->scope_depth--;

    return rc;
}

err_t nasm_lower_vdecl(nasm_lower_t* l, const ast_node_t* vd)
{
    BE_CHECK(l->be,
             vd->u.vdecl.type == AST_TYPE_INT ||
             vd->u.vdecl.type == AST_TYPE_PTR ||
             vd->u.vdecl.type == AST_TYPE_FLOAT,
             vd, "supports only int/ptr/float locals");

    size_t slot = 0;
    err_t rc = ir_add_slot(l->f, vd->u.vdecl.name_id, vd->u.vdecl.type, &slot);
    if (rc != OK)
        return rc;

    rc = be_bind_push(l->be, vd->u.vdecl.name_id, vd->u.vdecl.type,
                      slot, l->be->scope_depth);
    if (rc != OK)
        return rc;

    int f64 = vd->u.vdecl.type == AST_TYPE_FLOAT;
    ir_vreg_t val = { .id = IR_NO_VREG, .type = IR_TYPE_VOID };

    if (vd->left)
        rc = f64 ? nasm_lower_expr_f64(l, vd->left, &val)
                 : nasm_lower_expr_i64(l, vd->left, &val);
    else
    {
        val = ir_new_vreg(l->f, f64 ? IR_TYPE_F64 : IR_TYPE_I64);
        rc = ir_emit(l->f, (ir_instr_t){
            .op  = f64 ? IR_OP_MOV_IMM_F64 : IR_OP_MOV_IMM_I64,
            .dst = val,
            .imm = 0,
            .pos = vd->pos,
        });
    }

    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op   = IR_OP_STORE_SLOT,
        .slot = slot,
        .a    = val,
        .pos  = vd->pos,
    });
}

err_t nasm_lower_assign(nasm_lower_t* l, const ast_node_t* asn)
{
    ssize_t bi = be_bind_lookup(l->be, asn->u.assign.name_id);
    BE_CHECK(l->be, bi >= 0, asn, "Assignment to unknown variable");

    const binding_t* b = &l->be->binds[(size_t)bi];

    if (b->type == AST_TYPE_INT || b->type == AST_TYPE_PTR)
    {
        i64_t imm = 0;

        if (nasm_match_self_add_imm_(asn, asn->u.assign.name_id, &imm))
            return ir_emit(l->f, (ir_instr_t){
                .op   = IR_OP_ADD_SLOT_IMM_I64,
                .slot = b->offset,
                .imm  = imm,
                .pos  = asn->pos,
            });
    }

    ir_vreg_t val = { 0 };
    err_t rc = b->type == AST_TYPE_FLOAT
             ? nasm_lower_expr_f64(l, asn->left, &val)
             : nasm_lower_expr_i64(l, asn->left, &val);

    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op   = IR_OP_STORE_SLOT,
        .slot = b->offset,
        .a    = val,
        .pos  = asn->pos,
    });
}

err_t nasm_lower_return(nasm_lower_t* l, const ast_node_t* ret)
{
    if (l->meta->ret_type == AST_TYPE_INT ||
        l->meta->ret_type == AST_TYPE_PTR)
    {
        i64_t imm = 0;

        if (nasm_ast_i64_literal(ret->left, &imm))
            return ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_RET_IMM_I64,
                .imm = imm,
                .pos = ret->pos,
            });
    }

    if (ast_type_is_void(l->meta->ret_type))
        return ir_emit(l->f, (ir_instr_t){
            .op       = IR_OP_JMP,
            .label_id = l->ret_label,
            .pos      = ret->pos,
        });

    ir_vreg_t val = { 0 };
    err_t rc = l->meta->ret_type == AST_TYPE_FLOAT
             ? nasm_lower_expr_f64(l, ret->left, &val)
             : nasm_lower_expr_i64(l, ret->left, &val);

    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_RET,
        .a   = val,
        .pos = ret->pos,
    });
}

err_t nasm_lower_print_f64(nasm_lower_t* l, const ast_node_t* pr)
{
    ir_vreg_t val = { 0 };

    err_t rc = nasm_lower_expr_f64(l, pr->left, &val);
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_PRINTF_F64,
        .a   = val,
        .pos = pr->pos,
    });
}

err_t nasm_lower_print_i64(nasm_lower_t* l, const ast_node_t* pr)
{
    ir_vreg_t val = { 0 };

    err_t rc = nasm_lower_expr_i64(l, pr->left, &val);
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_PRINTF_I64,
        .a   = val,
        .pos = pr->pos,
    });
}

err_t nasm_lower_call_stmt(nasm_lower_t* l, const ast_node_t* st)
{
    const ast_node_t* call = st->left;

    BE_CHECK(l->be, call && call->kind == ASTK_CALL,
             st, "CALL_STMT without CALL child");

    ast_type_t ty = nasm_infer_expr_type(l->be, call);
    ir_vreg_t ignored = { 0 };

    if (ty == AST_TYPE_FLOAT)
        return nasm_lower_expr_f64(l, call, &ignored);

    return nasm_lower_expr_i64(l, call, &ignored);
}
