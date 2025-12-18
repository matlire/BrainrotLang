#include "reverse-frontend.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static err_t rf_fail_(operational_data_t* op, size_t pos, err_t rc, const char* msg)
{
    if (op)
    {
        op->error_pos = pos;
        if (op->error_msg[0] == '\0' && msg)
            snprintf(op->error_msg, sizeof(op->error_msg), "%s", msg);
    }
    return rc;
}

static const char* rf_type_kw_(ast_type_t t)
{
    switch (t)
    {
        case AST_TYPE_INT:   return "npc";
        case AST_TYPE_FLOAT: return "homie";
        case AST_TYPE_PTR:   return "sus";
        case AST_TYPE_VOID:  return "simp";
        default:             return "<type?>";
    }
}

static const char* rf_builtin_kw_(ast_builtin_unary_t id)
{
    switch (id)
    {
        case AST_BUILTIN_FLOOR: return "stan";
        case AST_BUILTIN_CEIL:  return "aura";
        case AST_BUILTIN_ROUND: return "delulu";
        case AST_BUILTIN_ITOF:  return "goober";
        case AST_BUILTIN_FTOI:  return "bozo";
        default:                return "stan";
    }
}

static void rf_indent_(FILE* out, int indent)
{
    for (int i = 0; i < indent; ++i)
        fprintf(out, "\t");
}

static int rf_expr_prec_(const ast_node_t* n)
{
    if (!n) return 100;

    switch (n->kind)
    {
        case ASTK_BINARY:
            switch (n->u.binary.op)
            {
                case TOK_OP_OR:    return 10;
                case TOK_OP_AND:   return 20;
                case TOK_OP_EQ:
                case TOK_OP_NEQ:   return 30;
                case TOK_OP_GT:
                case TOK_OP_LT:
                case TOK_OP_GTE:
                case TOK_OP_LTE:   return 40;
                case TOK_OP_PLUS:
                case TOK_OP_MINUS: return 50;
                case TOK_OP_MUL:
                case TOK_OP_DIV:   return 60;
                case TOK_OP_POW:   return 70;
                default:           return 55;
            }

        case ASTK_UNARY:
        case ASTK_BUILTIN_UNARY:
            return 80;

        case ASTK_CALL:
        case ASTK_IDENT:
        case ASTK_NUM_LIT:
        case ASTK_STR_LIT:
            return 90;

        default:
            return 90;
    }
}

static err_t rf_emit_expr_(operational_data_t* op, const ast_tree_t* t,
                           const ast_node_t* n, int parent_prec, int is_right_child);

static err_t rf_emit_str_lit_(operational_data_t* op, FILE* out, const ast_node_t* n)
{
    (void)op;

    fprintf(out, "\"");

    if (n && n->u.str.ptr && n->u.str.len > 0)
    {
        for (size_t i = 0; i < n->u.str.len; ++i)
        {
            unsigned char c = (unsigned char)n->u.str.ptr[i];
            switch (c)
            {
                case '\\': fprintf(out, "\\\\"); break;
                case '"':  fprintf(out, "\\\""); break;
                case '\n': fprintf(out, "\\n");  break;
                case '\t': fprintf(out, "\\t");  break;
                case '\r': fprintf(out, "\\r");  break;
                case '\0': fprintf(out, "\\0");  break;
                default:
                    if (c >= 32 && c < 127)
                        fprintf(out, "%d", (int)c);
                    else
                        fprintf(out, "\\x%02X", (unsigned)c);
                    break;
            }
        }
    }

    fprintf(out, "\"");
    return OK;
}

static err_t rf_emit_arg_list_(operational_data_t* op, const ast_tree_t* t, const ast_node_t* args)
{
    if (!op || !op->out_file) return ERR_BAD_ARG;

    int first = 1;
    for (const ast_node_t* c = args ? args->left : NULL; c; c = c->right)
    {
        if (!first) fprintf(op->out_file, ", ");
        first = 0;

        err_t rc = rf_emit_expr_(op, t, c, 0, 0);
        if (rc != OK) return rc;
    }
    return OK;
}

static err_t rf_emit_call_(operational_data_t* op, const ast_tree_t* t, const ast_node_t* call)
{
    if (!op || !op->out_file || !t || !call) return ERR_BAD_ARG;

    const char* name = ast_name_cstr(t, call->u.call.name_id);
    if (!name) name = "<fn?>";

    fprintf(op->out_file, "%s(", name);

    const ast_node_t* args = call->left;
    err_t rc = rf_emit_arg_list_(op, t, args);
    if (rc != OK) return rc;

    fprintf(op->out_file, ")");
    return OK;
}

static err_t rf_emit_expr_(operational_data_t* op, const ast_tree_t* t,
                           const ast_node_t* n, int parent_prec, int is_right_child)
{
    if (!op || !op->out_file) return ERR_BAD_ARG;
    if (!t) return ERR_BAD_ARG;
    if (!n) return rf_fail_(op, 0, ERR_SYNTAX, "Expression node is NULL");

    FILE* out = op->out_file;

    int my_prec = rf_expr_prec_(n);
    int need_parens = (my_prec < parent_prec) || (is_right_child && my_prec == parent_prec);

    if (need_parens) fprintf(out, "(");

    switch (n->kind)
    {
        case ASTK_IDENT:
        {
            const char* name = ast_name_cstr(t, n->u.ident.name_id);
            if (!name) name = "<id?>";
            fprintf(out, "%s", name);
        } break;

        case ASTK_NUM_LIT:
        {
            if (n->u.num.lit_type == LIT_INT)
                fprintf(out, "%lld", (long long)n->u.num.lit.i64);
            else if (n->u.num.lit_type == LIT_FLOAT)
                fprintf(out, "%g", n->u.num.lit.f64);
            else
                fprintf(out, "0");
        } break;

        case ASTK_STR_LIT:
        {
            err_t rc = rf_emit_str_lit_(op, out, n);
            if (rc != OK) return rc;
        } break;

        case ASTK_CALL:
        {
            err_t rc = rf_emit_call_(op, t, n);
            if (rc != OK) return rc;
        } break;

        case ASTK_BUILTIN_UNARY:
        {
            const char* kw = rf_builtin_kw_(n->u.builtin_unary.id);
            fprintf(out, "%s(", kw);

            const ast_node_t* arg = n->left;
            if (!arg) return rf_fail_(op, n->pos.offset, ERR_CORRUPT, "BUILTIN_UNARY has no argument");

            err_t rc = rf_emit_expr_(op, t, arg, 0, 0);
            if (rc != OK) return rc;

            fprintf(out, ")");
        } break;

        case ASTK_UNARY:
        {
            const char* opstr = token_kind_to_cstr(n->u.unary.op);
            if (!opstr) opstr = "?";
            fprintf(out, opstr);

            const ast_node_t* rhs = n->left;
            if (!rhs) return rf_fail_(op, n->pos.offset, ERR_CORRUPT, "UNARY has no operand");

            int rhs_prec = rf_expr_prec_(rhs);
            int rhs_parens = (rhs->kind == ASTK_BINARY) || (rhs_prec < 80);

            if (rhs_parens) fprintf(out, "(");
            err_t rc = rf_emit_expr_(op, t, rhs, 80, 0);
            if (rc != OK) return rc;
            if (rhs_parens) fprintf(out, ")");
        } break;

        case ASTK_BINARY:
        {
            const ast_node_t* a = n->left;
            const ast_node_t* b = a ? a->right : NULL;

            if (!a || !b)
                return rf_fail_(op, n->pos.offset, ERR_CORRUPT, "BINARY must have two operands");

            int p = rf_expr_prec_(n);
            err_t rc = rf_emit_expr_(op, t, a, p, 0);
            if (rc != OK) return rc;

            const char* opstr = token_kind_to_cstr(n->u.binary.op);
            if (!opstr) opstr = "?";
            fprintf(out, " %s ", opstr);

            rc = rf_emit_expr_(op, t, b, p, 1);
            if (rc != OK) return rc;
        } break;

        default:
            return rf_fail_(op, n->pos.offset, ERR_CORRUPT, "Unexpected node kind in expression");
    }

    if (need_parens) fprintf(out, ")");
    return OK;
}

static err_t rf_emit_stmt_(operational_data_t* op, const ast_tree_t* t,
                           const ast_node_t* st, int indent);

static err_t rf_emit_block_(operational_data_t* op, const ast_tree_t* t,
                            const ast_node_t* block, int indent)
{
    if (!block || block->kind != ASTK_BLOCK)
        return rf_fail_(op, block ? block->pos.offset : 0, ERR_CORRUPT, "Expected BLOCK");

    FILE* out = op->out_file;

    rf_indent_(out, indent);
    fprintf(out, "yap\n");

    for (const ast_node_t* c = block->left; c; c = c->right)
    {
        err_t rc = rf_emit_stmt_(op, t, c, indent + 1);
        if (rc != OK) return rc;
    }

    rf_indent_(out, indent);
    fprintf(out, "yapity\n");
    return OK;
}

static err_t rf_emit_if_chain_(operational_data_t* op, const ast_tree_t* t,
                               const ast_node_t* ifn, int indent)
{
    const ast_node_t* cond = ifn->left;
    const ast_node_t* then_st = cond ? cond->right : NULL;
    const ast_node_t* tail = then_st ? then_st->right : NULL;

    if (!cond || !then_st)
        return rf_fail_(op, ifn->pos.offset, ERR_CORRUPT, "IF must have (cond, then)");

    FILE* out = op->out_file;

    rf_indent_(out, indent);
    fprintf(out, "alpha (");
    err_t rc = rf_emit_expr_(op, t, cond, 0, 0);
    if (rc != OK) return rc;
    fprintf(out, ")\n");

    rc = rf_emit_stmt_(op, t, then_st, indent + 1);
    if (rc != OK) return rc;

    const ast_node_t* cur = tail;
    while (cur)
    {
        if (cur->kind == ASTK_BRANCH)
        {
            const ast_node_t* bcond = cur->left;
            const ast_node_t* bstmt = bcond ? bcond->right : NULL;
            const ast_node_t* next  = bstmt ? bstmt->right : NULL;

            if (!bcond || !bstmt)
                return rf_fail_(op, cur->pos.offset, ERR_CORRUPT, "BRANCH must have (cond, stmt)");

            rf_indent_(out, indent);
            fprintf(out, "omega (");
            rc = rf_emit_expr_(op, t, bcond, 0, 0);
            if (rc != OK) return rc;
            fprintf(out, ")\n");

            rc = rf_emit_stmt_(op, t, bstmt, indent + 1);
            if (rc != OK) return rc;

            cur = next;
            continue;
        }

        if (cur->kind == ASTK_ELSE)
        {
            const ast_node_t* eb = cur->left;
            if (!eb)
                return rf_fail_(op, cur->pos.offset, ERR_CORRUPT, "ELSE must have body");

            rf_indent_(out, indent);
            fprintf(out, "sigma\n");

            rc = rf_emit_stmt_(op, t, eb, indent + 1);
            if (rc != OK) return rc;

            cur = NULL;
            continue;
        }

        return rf_fail_(op, cur->pos.offset, ERR_CORRUPT, "IF tail is neither BRANCH nor ELSE");
    }

    return OK;
}

static err_t rf_emit_stmt_(operational_data_t* op, const ast_tree_t* t,
                           const ast_node_t* st, int indent)
{
    if (!op || !op->out_file || !t || !st) return ERR_BAD_ARG;

    FILE* out = op->out_file;

    switch (st->kind)
    {
        case ASTK_BLOCK:
            return rf_emit_block_(op, t, st, indent);

        case ASTK_WHILE:
        {
            const ast_node_t* cond = st->left;
            const ast_node_t* body = cond ? cond->right : NULL;

            if (!cond || !body)
                return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "WHILE must have (cond, body)");

            rf_indent_(out, indent);
            fprintf(out, "lowkey (");
            err_t rc = rf_emit_expr_(op, t, cond, 0, 0);
            if (rc != OK) return rc;
            fprintf(out, ")\n");

            return rf_emit_stmt_(op, t, body, indent + 1);
        }

        case ASTK_IF:
            return rf_emit_if_chain_(op, t, st, indent);

        case ASTK_VAR_DECL:
        {
            const char* name = ast_name_cstr(t, st->u.vdecl.name_id);
            if (!name) name = "<var?>";

            rf_indent_(out, indent);
            fprintf(out, "%s %s", rf_type_kw_(st->u.vdecl.type), name);

            const ast_node_t* init = st->left;
            if (init)
            {
                fprintf(out, " gaslight ");
                err_t rc = rf_emit_expr_(op, t, init, 0, 0);
                if (rc != OK) return rc;
            }

            fprintf(out, ";\n");
            return OK;
        }

        case ASTK_ASSIGN:
        {
            const char* name = ast_name_cstr(t, st->u.assign.name_id);
            if (!name) name = "<id?>";

            const ast_node_t* rhs = st->left;
            if (!rhs)
                return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "ASSIGN must have rhs");

            rf_indent_(out, indent);
            fprintf(out, "%s gaslight ", name);

            err_t rc = rf_emit_expr_(op, t, rhs, 0, 0);
            if (rc != OK) return rc;

            fprintf(out, ";\n");
            return OK;
        }

        case ASTK_BREAK:
            rf_indent_(out, indent);
            fprintf(out, "gg;\n");
            return OK;

        case ASTK_RETURN:
        {
            rf_indent_(out, indent);
            fprintf(out, "micdrop");

            const ast_node_t* e = st->left;
            if (e)
            {
                fprintf(out, " ");
                err_t rc = rf_emit_expr_(op, t, e, 0, 0);
                if (rc != OK) return rc;
            }

            fprintf(out, ";\n");
            return OK;
        }

        case ASTK_CALL_STMT:
        {
            const ast_node_t* call = st->left; /* ASTK_CALL */
            if (!call || call->kind != ASTK_CALL)
                return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "CALL_STMT must contain CALL");

            rf_indent_(out, indent);
            fprintf(out, "bruh ");

            err_t rc = rf_emit_call_(op, t, call);
            if (rc != OK) return rc;

            fprintf(out, ";\n");
            return OK;
        }

        case ASTK_COUT:
        case ASTK_ICOUT:
        case ASTK_FCOUT:
        {
            const ast_node_t* e = st->left;
            if (!e)
                return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "COUT/ICOUT/FCOUT must have expr");

            const char* kw =
                (st->kind == ASTK_COUT)  ? "based" :
                (st->kind == ASTK_ICOUT) ? "mid"   : "peak";

            rf_indent_(out, indent);
            fprintf(out, "%s(", kw);

            err_t rc = rf_emit_expr_(op, t, e, 0, 0);
            if (rc != OK) return rc;

            fprintf(out, ");\n");
            return OK;
        }

        case ASTK_EXPR_STMT:
        {
            const ast_node_t* e = st->left;
            if (!e)
                return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "EXPR_STMT must have expr");

            rf_indent_(out, indent);

            err_t rc = rf_emit_expr_(op, t, e, 0, 0);
            if (rc != OK) return rc;

            fprintf(out, ";\n");
            return OK;
        }

        case ASTK_EMPTY:
            return OK;

        default:
            return rf_fail_(op, st->pos.offset, ERR_CORRUPT, "Unknown/unsupported statement node");
    }
}

static err_t rf_emit_param_list_(operational_data_t* op, const ast_tree_t* t, const ast_node_t* plist)
{
    if (!op || !op->out_file) return ERR_BAD_ARG;
    if (!plist || plist->kind != ASTK_PARAM_LIST)
        return rf_fail_(op, plist ? plist->pos.offset : 0, ERR_CORRUPT, "Expected PARAM_LIST");

    int first = 1;
    for (const ast_node_t* p = plist->left; p; p = p->right)
    {
        if (p->kind != ASTK_PARAM)
            return rf_fail_(op, p->pos.offset, ERR_CORRUPT, "PARAM_LIST contains non-PARAM");

        const char* name = ast_name_cstr(t, p->u.param.name_id);
        if (!name) name = "<param?>";

        if (!first) fprintf(op->out_file, ", ");
        first = 0;

        fprintf(op->out_file, "%s %s", rf_type_kw_(p->u.param.type), name);
    }
    return OK;
}

static err_t rf_emit_func_(operational_data_t* op, const ast_tree_t* t, const ast_node_t* fn)
{
    if (!fn || fn->kind != ASTK_FUNC)
        return rf_fail_(op, fn ? fn->pos.offset : 0, ERR_CORRUPT, "Expected FUNC");

    const ast_node_t* plist = fn->left;
    const ast_node_t* body  = plist ? plist->right : NULL;

    if (!plist || !body)
        return rf_fail_(op, fn->pos.offset, ERR_CORRUPT, "FUNC must have (PARAM_LIST, BLOCK)");

    const char* name = ast_name_cstr(t, fn->u.func.name_id);
    if (!name) name = "<fn?>";

    fprintf(op->out_file, "%s %s(", rf_type_kw_(fn->u.func.ret_type), name);

    err_t rc = rf_emit_param_list_(op, t, plist);
    if (rc != OK) return rc;

    fprintf(op->out_file, ")\n");

    rc = rf_emit_stmt_(op, t, body, 0);
    if (rc != OK) return rc;

    fprintf(op->out_file, "\n");
    return OK;
}

static err_t rf_emit_program_(operational_data_t* op, const ast_tree_t* t, const ast_node_t* root)
{
    if (!root || root->kind != ASTK_PROGRAM)
        return rf_fail_(op, root ? root->pos.offset : 0, ERR_CORRUPT, "AST root must be PROGRAM");

    int any = 0;
    for (const ast_node_t* fn = root->left; fn; fn = fn->right)
    {
        if (fn->kind != ASTK_FUNC)
            return rf_fail_(op, fn->pos.offset, ERR_CORRUPT, "PROGRAM contains non-FUNC");

        err_t rc = rf_emit_func_(op, t, fn);
        if (rc != OK) return rc;

        any = 1;
    }

    if (!any)
        return rf_fail_(op, root->pos.offset, ERR_SYNTAX, "PROGRAM has no functions");

    return OK;
}

err_t reverse_frontend_write_rot(operational_data_t* op, const ast_tree_t* ast_tree)
{
    if (!op || !ast_tree) return ERR_BAD_ARG;
    if (!op->out_file)    return rf_fail_(op, 0, ERR_BAD_ARG, "op->out_file is NULL");
    if (!ast_tree->root)  return rf_fail_(op, 0, ERR_SYNTAX,  "AST root is NULL");

    op->error_pos = 0;
    op->error_msg[0] = '\0';

    return rf_emit_program_(op, ast_tree, ast_tree->root);
}
