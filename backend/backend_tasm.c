#include "backend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int be_type_is_float_(ast_type_t type)
{
    return type == AST_TYPE_FLOAT;
}

static int be_type_has_value_(ast_type_t type)
{
    return type != AST_TYPE_UNKNOWN && !ast_type_is_void(type);
}

static int be_type_is_stack_scalar_(ast_type_t type)
{
    return ast_type_is_scalar(type);
}

static void be_emit_cast_top_(backend_t* be, ast_type_t from, ast_type_t to)
{
    if (!be || from == to || to == AST_TYPE_UNKNOWN || ast_type_is_void(to))
        return;

    if (to == AST_TYPE_FLOAT && from != AST_TYPE_FLOAT)
    {
        be_emitf(be, "ITOF\n");
        return;
    }

    if (to != AST_TYPE_FLOAT && from == AST_TYPE_FLOAT)
    {
        be_emitf(be, "FTOI\n");
        return;
    }
}

static ast_type_t be_literal_type_(const ast_node_t* node)
{
    if (!node || node->kind != ASTK_NUM_LIT)
        return AST_TYPE_UNKNOWN;

    return (node->u.num.lit_type == LIT_FLOAT) ? AST_TYPE_FLOAT : AST_TYPE_INT;
}

static err_t be_emit_program_  (backend_t* be, const ast_node_t* program);

static err_t be_emit_func_     (backend_t* be, const ast_node_t* fn);

static err_t be_emit_stmt_     (backend_t* be, const ast_node_t* st);
static err_t be_emit_block_    (backend_t* be, const ast_node_t* block);
static err_t be_emit_while_    (backend_t* be, const ast_node_t* w);
static err_t be_emit_if_chain_ (backend_t* be, const ast_node_t* ifnode);
static err_t be_emit_return_   (backend_t* be, const ast_node_t* r);
static err_t be_emit_break_    (backend_t* be, const ast_node_t* brk);
static err_t be_emit_vdecl_    (backend_t* be, const ast_node_t* vd);
static err_t be_emit_assign_   (backend_t* be, const ast_node_t* asn);
static err_t be_emit_print_    (backend_t* be, const ast_node_t* pr);
static err_t be_emit_call_stmt_(backend_t* be, const ast_node_t* cs);
static err_t be_emit_expr_stmt_(backend_t* be, const ast_node_t* es);

static err_t be_emit_builtin_call_(backend_t* be, const ast_node_t* call, ast_type_t* out_type);
static err_t be_emit_expr_        (backend_t* be, const ast_node_t* e, ast_type_t* out_type);

static void be_emit_addr_bp_off_(backend_t* be, size_t offset)
{
    // x13 = x15
    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_BP);
    be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);

    // x13 = x13 + offset
    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
    be_emitf(be, "PUSH  %zu\n", offset);
    be_emitf(be, "ADD\n");
    be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);
}

static void be_emit_load_bp_off_(backend_t* be, size_t offset)
{
    be_emit_addr_bp_off_(be, offset);
    be_emitf(be, "PUSHM x%u\n", (unsigned)REG_TMPA);
}

static void be_emit_store_bp_off_(backend_t* be, size_t offset)
{
    be_emit_addr_bp_off_(be, offset);
    be_emitf(be, "POPM x%u\n", (unsigned)REG_TMPA);
}

// Compute addr = SP + imm into x13
static void be_emit_addr_sp_plus_(backend_t* be, size_t imm)
{
    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_SP);
    be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);

    if (imm != 0)
    {
        be_emitf(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
        be_emitf(be, "PUSH  %zu\n", imm);
        be_emitf(be, "ADD\n");
        be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);
    }
}

err_t backend_emit_tasm(const ast_tree_t* tree, operational_data_t* op_data)
{
    if (!tree || !tree->root || !op_data)
        return ERR_BAD_ARG;

    backend_t be = { 0 };
    be.tree      = tree;
    be.op        = op_data;

    const ast_node_t* program = tree->root;

    if (program->kind != ASTK_PROGRAM)
    {
        be_set_error(&be, program->pos, program->pos.offset,
                     "Root is not PROGRAM");
        return ERR_SYNTAX;
    }

    err_t rc = be_collect_funcs(&be, program, ":fn_%s");
    if (rc != OK)
        goto cleanup;

    size_t main_id = be_find_name(tree, "main");
    if (main_id == SIZE_MAX || !be_find_func(&be, main_id))
    {
        be_set_error(&be, (token_pos_t){1, 1, 0}, 0,
                     "No function 'main' found");
        rc = ERR_SYNTAX;
        goto cleanup;
    }

    rc = be_emit_program_(&be, program);

cleanup:
    be_free(&be);
    return rc;
}

static err_t be_emit_program_(backend_t* be, const ast_node_t* program)
{
    // init SP/BP = 0; CALL main; HLT
    be_emitf(be, "; --- program entry ---\n");
    be_emitf(be, "PUSH 0\nPOPR x%u\n", (unsigned)REG_SP);
    be_emitf(be, "PUSH 0\nPOPR x%u\n", (unsigned)REG_BP);

    // CALL :fn_main
    {
        size_t main_id = SIZE_MAX;
        for (size_t i = 0; i < be->tree->nametable.amount; ++i)
            if (be->tree->nametable.data[i].name && strcmp(be->tree->nametable.data[i].name, "main") == 0)
                { main_id = i; break; }

        const func_meta_t* fm = be_find_func(be, main_id);
        BE_CHECK(be, fm != NULL, program, "No main() metadata");
        be_emitf(be, "CALL %s\n", fm->label);
    }

    be_emitf(be, "HLT\n\n");

    // emit all functions
    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        err_t rc = be_emit_func_(be, fn);
        if (rc != OK) return rc;
        be_emitf(be, "\n");
    }

    return OK;
}

static err_t be_emit_func_(backend_t* be, const ast_node_t* fn)
{
    BE_CHECK(be, fn && fn->kind == ASTK_FUNC, fn, "Internal: expected FUNC");
    const func_meta_t* meta = be_find_func(be, fn->u.func.name_id);
    BE_CHECK(be, meta != NULL, fn, "Internal: no metadata for function '%s'",
             ast_name_cstr(be->tree, fn->u.func.name_id));

    be->cur_fn = meta;

    free(be->fn_end_label);
    be->fn_end_label = be_new_label(be, "fn_end", ":L_");
    if (!be->fn_end_label) return ERR_ALLOC;

    // reset bindings
    be->bind_amount  = 0;
    be->scope_depth  = 1;
    be->next_local_offset = 1 + meta->param_count;

    {
        const ast_node_t* plist = fn->left;
        size_t i = 0;
        for (const ast_node_t* p = plist ? plist->left : NULL; p; p = p->right)
        {
            // param node has name_id + type
            err_t rc = be_bind_push(be, p->u.param.name_id, p->u.param.type, 1 + i, be->scope_depth);
            if (rc != OK) return rc;
            i++;
        }
    }

    be_emitf(be, "; --- function %s ---\n", ast_name_cstr(be->tree, meta->name_id));
    be_emitf(be, "%s\n", meta->label);

    //   RAM[SP] = oldBP
    //   BP = SP
    //   SP = SP + (1 + param_count + local_count)
    //
    // Using: x13 as addr temp, x14=SP, x15=BP
    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_BP);        // push old BP
    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_SP);        // copy SP into x13
    be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);
    be_emitf(be, "POPM  x%u\n", (unsigned)REG_TMPA);      // RAM[SP] = oldBP

    be_emitf(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_SP, (unsigned)REG_BP); // BP = SP

    const size_t frame = 1 + meta->param_count + meta->local_count;
    be_emitf(be, "PUSHR x%u\nPUSH %zu\nADD\nPOPR x%u\n",
              (unsigned)REG_SP, frame, (unsigned)REG_SP);

    const ast_node_t* plist = fn->left;
    const ast_node_t* body  = plist ? plist->right : NULL;
    if (!body) body = fn->left ? fn->left->right : NULL;

    BE_CHECK(be, body != NULL, fn, "Function has no body");
    err_t rc = be_emit_stmt_(be, body);
    if (rc != OK) return rc;

    if (!ast_type_is_void(meta->ret_type))
    {
        be_emitf(be, "; implicit return 0 (defensive)\n");
        be_emitf(be, "PUSH 0\n");
        if (meta->ret_type == AST_TYPE_FLOAT)
        {
            be_emitf(be, "ITOF\n");
            be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_RET_F);
        }
        else
            be_emitf(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }

    be_emitf(be, "%s\n", be->fn_end_label);

    // SP = BP
    be_emitf(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_BP, (unsigned)REG_SP);

    // BP = RAM[BP]
    be_emitf(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_BP, (unsigned)REG_TMPA);
    be_emitf(be, "PUSHM x%u\n", (unsigned)REG_TMPA);
    be_emitf(be, "POPR  x%u\n", (unsigned)REG_BP);

    be_emitf(be, "RET\n");

    return OK;
}

#define BE_BUILTIN0_RET(BNAME, INSTR, RETTYPE)                      \
    block_begin                                                     \
        if (be_streq(name, BNAME)) {                                \
            BE_CHECK(be, argc == 0, call, BNAME "() takes 0 args"); \
            be_emitf(be, INSTR "\n");                               \
            if (out_type) *out_type = (RETTYPE);                    \
            return OK;                                              \
        }                                                           \
    block_end

#define BE_BUILTIN0_VOID(BNAME, INSTR)                              \
    block_begin                                                     \
        if (be_streq(name, BNAME)) {                                \
            BE_CHECK(be, argc == 0, call, BNAME "() takes 0 args"); \
            be_emitf(be, INSTR "\n");                               \
            if (out_type) *out_type = AST_TYPE_VOID;                \
            return OK;                                              \
        }                                                           \
    block_end

static size_t arg_count_(const ast_node_t* args)
{
    if (!args) return 0;
    size_t n = 0;
    for (const ast_node_t* a = args->left; a; a = a->right) n++;
    return n;
}

static const ast_node_t* arg_at_(const ast_node_t* args, size_t idx)
{
    if (!args) return NULL;
    const ast_node_t* a = args->left;
    while (a && idx--) a = a->right;
    return a;
}

static err_t be_emit_builtin_call_(backend_t* be, const ast_node_t* call, ast_type_t* out_type)
{
    const char* name = ast_name_cstr(be->tree, call->u.call.name_id);
    const ast_node_t* args = call->left;
    const size_t argc = arg_count_(args);

    BE_BUILTIN0_RET ("in",       "IN",      AST_TYPE_INT);
    BE_BUILTIN0_RET ("fin",      "FIN",     AST_TYPE_FLOAT);
    BE_BUILTIN0_RET ("cin",      "CIN",     AST_TYPE_INT);

    BE_BUILTIN0_VOID("draw",     "DRAW");
    BE_BUILTIN0_VOID("clean_vm", "CLEANVM");

    BE_BUILTIN0_RET ("cap",      "IN",      AST_TYPE_INT);
    BE_BUILTIN0_RET ("nocap",    "FIN",     AST_TYPE_FLOAT);
    BE_BUILTIN0_RET ("stinky",   "CIN",     AST_TYPE_INT);

    BE_BUILTIN0_VOID("gyat",     "DRAW");
    BE_BUILTIN0_VOID("skibidi",  "CLEANVM");

    err_t rc = ERR_BAD_ARG;
    ast_type_t ret_type = AST_TYPE_UNKNOWN;

    if (be_streq(name, "out")  || be_streq(name, "fout") || be_streq(name, "cout") ||
        be_streq(name, "pookie") || be_streq(name, "rizz") || be_streq(name, "menace"))
    {
        BE_CHECK(be, argc == 1, call, "%s() takes 1 arg", name);

        ast_type_t at = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &at) == OK, call, "bad arg");

        const int is_fout = be_streq(name, "fout") || be_streq(name, "rizz");
        const int is_cout = be_streq(name, "cout") || be_streq(name, "menace");

        if (is_fout)
        {
            if (!be_type_is_float_(at)) be_emitf(be, "ITOF\n");
            be_emitf(be, "FTOPOUT\n");
            ret_type = AST_TYPE_FLOAT;
        }
        else
        {
            if (be_type_is_float_(at)) be_emitf(be, "FTOI\n");
            be_emitf(be, is_cout ? "CTOPOUT\n" : "TOPOUT\n");
            ret_type = AST_TYPE_INT;
        }

        rc = OK;
    }
    else if (be_streq(name, "set_pixel"))
    {
        BE_CHECK(be, argc == 3, call, "set_pixel(x,y,ch) takes 3 args");

        // addr = y*W + x
        ast_type_t ty = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 1), &ty) == OK, call, "bad y");
        if (be_type_is_float_(ty)) be_emitf(be, "FTOI\n");
        be_emitf(be, "PUSH %d\nMUL\n", (int)BE_SCREEN_WIDTH);

        ast_type_t tx = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &tx) == OK, call, "bad x");
        if (be_type_is_float_(tx)) be_emitf(be, "FTOI\n");
        be_emitf(be, "ADD\nPOPR x%u\n", (unsigned)REG_TMPA); // x13 = addr

        ast_type_t tch = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 2), &tch) == OK, call, "bad ch");
        if (be_type_is_float_(tch)) be_emitf(be, "FTOI\n");
        be_emitf(be, "POPVM x%u\n", (unsigned)REG_TMPA);

        ret_type = AST_TYPE_VOID;
        rc = OK;
    }
    else if (be_streq(name, "sqrt"))
    {
        BE_CHECK(be, argc == 1, call, "sqrt(x) takes 1 arg");

        ast_type_t at = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &at) == OK, call, "bad arg");

        if (!be_type_is_float_(at))
            be_emitf(be, "ITOF\n");

        // sqrt(x) = x ^ 0.5
        be_emitf(be, "PUSH 0.5\n");
        be_emitf(be, "FPOWF\n");

        ret_type = AST_TYPE_FLOAT;
        rc = OK;
    }
    else if (be_streq(name, "pow") || be_streq(name, "mpow"))
    {
        BE_CHECK(be, argc == 2, call, "%s(x,y) takes 2 args", name);

        ast_type_t at = AST_TYPE_UNKNOWN;
        ast_type_t bt = AST_TYPE_UNKNOWN;

        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &at) == OK, call, "bad base");
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 1), &bt) == OK, call, "bad exponent");

        if      (at == AST_TYPE_INT   && bt == AST_TYPE_INT)   be_emitf(be, "POW\n");
        else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_INT)   be_emitf(be, "FPOW\n");
        else if (at == AST_TYPE_INT   && bt == AST_TYPE_FLOAT) be_emitf(be, "POWF\n");
        else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_FLOAT) be_emitf(be, "FPOWF\n");
        else
            BE_FAIL_NODE(be, call, "Unsupported types for %s(x,y)", name);

        ret_type = (at == AST_TYPE_INT && bt == AST_TYPE_INT) ? AST_TYPE_INT : AST_TYPE_FLOAT;
        rc = OK;
    }
    else if (be_streq(name, "xor") || be_streq(name, "shl") || be_streq(name, "shr"))
    {
        BE_CHECK(be, argc == 2, call, "%s(x,y) takes 2 args", name);

        ast_type_t at = AST_TYPE_UNKNOWN;
        ast_type_t bt = AST_TYPE_UNKNOWN;

        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &at) == OK, call, "bad lhs");
        if (be_type_is_float_(at))
            be_emitf(be, "FTOI\n");

        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 1), &bt) == OK, call, "bad rhs");
        if (be_type_is_float_(bt))
            be_emitf(be, "FTOI\n");

        if      (be_streq(name, "xor")) be_emitf(be, "XOR\n");
        else if (be_streq(name, "shl")) be_emitf(be, "SHL\n");
        else                            be_emitf(be, "SHR\n");

        ret_type = AST_TYPE_INT;
        rc = OK;
    }

    if (rc == OK && out_type) *out_type = ret_type;
    return rc;
}

static err_t be_emit_stmt_(backend_t* be, const ast_node_t* st)
{
    if (!st) return OK;

    switch (st->kind)
    {
        case ASTK_BLOCK:     return be_emit_block_(be, st);
        case ASTK_WHILE:     return be_emit_while_(be, st);
        case ASTK_IF:        return be_emit_if_chain_(be, st);

        case ASTK_VAR_DECL:  return be_emit_vdecl_(be, st);
        case ASTK_ASSIGN:    return be_emit_assign_(be, st);

        case ASTK_RETURN:    return be_emit_return_(be, st);
        case ASTK_BREAK:     return be_emit_break_(be, st);

        case ASTK_EXPR_STMT: return be_emit_expr_stmt_(be, st);
        case ASTK_CALL_STMT: return be_emit_call_stmt_(be, st);

        case ASTK_COUT:
        case ASTK_ICOUT:
        case ASTK_FCOUT:     return be_emit_print_(be, st);

        default:
            BE_FAIL_NODE(be, st, "Backend: unsupported statement kind %s", ast_kind_to_cstr(st->kind));
    }
}

static err_t be_emit_block_(backend_t* be, const ast_node_t* block)
{
    BE_CHECK(be, block->kind == ASTK_BLOCK, block, "Internal: not a BLOCK");

    be->scope_depth++;
    size_t depth = be->scope_depth;

    for (const ast_node_t* c = block->left; c; c = c->right)
    {
        err_t rc = be_emit_stmt_(be, c);
        if (rc != OK) return rc;
    }

    be_bind_pop_depth(be, depth);
    be->scope_depth--;
    return OK;
}

static int is_bool_op_(token_kind_t k)
{
    return (k == TOK_OP_EQ || k == TOK_OP_NEQ ||
            k == TOK_OP_LT || k == TOK_OP_GT ||
            k == TOK_OP_LTE || k == TOK_OP_GTE);
}

static void be_promote_pair_to_float_(backend_t* be, ast_type_t* at, ast_type_t* bt)
{
    if (!be_type_is_float_(*bt))
    {
        be_emitf(be, "ITOF\n");
        *bt = AST_TYPE_FLOAT;
    }

    if (!be_type_is_float_(*at))
    {
        be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_TMP_F);
        be_emitf(be, "ITOF\n");
        be_emitf(be, "FPUSHR fx%u\n", (unsigned)REG_TMP_F);
        *at = AST_TYPE_FLOAT;
    }
}

static err_t be_emit_cond_jfalse_(backend_t* be, const ast_node_t* cond, const char* L_false)
{
    if (!cond) return OK;

    if (cond->kind == ASTK_BINARY && is_bool_op_(cond->u.binary.op))
    {
        const token_kind_t opk = cond->u.binary.op;
        const ast_node_t* a = cond->left;
        const ast_node_t* b = a ? a->right : NULL;
        BE_CHECK(be, a && b, cond, "Bad condition: missing operands");

        ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
        err_t rc = be_emit_expr_(be, a, &at); if (rc != OK) return rc;
        rc = be_emit_expr_(be, b, &bt); if (rc != OK) return rc;
        if (be_type_is_float_(at) || be_type_is_float_(bt))
        {
            be_promote_pair_to_float_(be, &at, &bt);
            be_emitf(be, "FCMP\n");

            switch (opk)
            {
                case TOK_OP_EQ:  be_emitf(be, "PUSH 0\nJNE %s\n", L_false); break;
                case TOK_OP_NEQ: be_emitf(be, "PUSH 0\nJE  %s\n", L_false); break;
                case TOK_OP_LT:  be_emitf(be, "PUSH -1\nJNE %s\n", L_false); break;
                case TOK_OP_LTE: be_emitf(be, "PUSH 1\nJE  %s\n", L_false); break;
                case TOK_OP_GT:  be_emitf(be, "PUSH 1\nJNE %s\n", L_false); break;
                case TOK_OP_GTE: be_emitf(be, "PUSH -1\nJE  %s\n", L_false); break;
                default: BE_FAIL_NODE(be, cond, "Unsupported float compare op");
            }
            return OK;
        }

        const char* jfalse = NULL;
        switch (opk)
        {
            case TOK_OP_EQ:  jfalse = "JNE"; break;
            case TOK_OP_NEQ: jfalse = "JE";  break;
            case TOK_OP_LT:  jfalse = "JAE"; break;
            case TOK_OP_LTE: jfalse = "JA";  break;
            case TOK_OP_GT:  jfalse = "JBE"; break;
            case TOK_OP_GTE: jfalse = "JB";  break;
            default: BE_FAIL_NODE(be, cond, "Unsupported int compare op");
        }

        be_emitf(be, "%s %s\n", jfalse, L_false);
        return OK;
    }

    ast_type_t ct = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, cond, &ct);
    if (rc != OK) return rc;

    if (be_type_is_float_(ct))
    {
        be_emitf(be, "PUSH 0\nITOF\n"); // 0.0
        be_emitf(be, "FCMP\n");         // compare cond vs 0.0 -> int
        be_emitf(be, "PUSH 0\n");       // res == 0 ?
        be_emitf(be, "JE %s\n", L_false);
    }
    else
    {
        be_emitf(be, "PUSH 0\n");
        be_emitf(be, "JE %s\n", L_false);
    }

    return OK;
}

static err_t be_emit_while_(backend_t* be, const ast_node_t* w)
{
    const ast_node_t* cond = w->left;
    const ast_node_t* body = cond ? cond->right : NULL;

    BE_CHECK(be, cond && body, w, "Internal: WHILE must have (cond, body)");

    char* L_begin = be_new_label(be, "while_begin", ":L_");
    char* L_end   = be_new_label(be, "while_end", ":L_");
    if (!L_begin || !L_end) { free(L_begin); free(L_end); return ERR_ALLOC; }

    // push loop ctx
    BE_VEC_GROW(be->loops, be->loop_cap, be->loop_amount + 1, loop_ctx_t);
    be->loops[be->loop_amount++] = (loop_ctx_t){ .end_label = strdup(L_end) };

    be_emitf(be, "%s\n", L_begin);

    err_t rc = be_emit_cond_jfalse_(be, cond, L_end);
    if (rc != OK) goto done;

    rc = be_emit_stmt_(be, body);
    if (rc != OK) goto done;

    be_emitf(be, "JMP %s\n", L_begin);
    be_emitf(be, "%s\n", L_end);

done:
    // pop loop ctx
    if (be->loop_amount > 0)
    {
        free(be->loops[be->loop_amount - 1].end_label);
        be->loop_amount--;
    }

    free(L_begin);
    free(L_end);
    return rc;
}

static err_t be_emit_break_(backend_t* be, const ast_node_t* brk)
{
    (void)brk;
    BE_CHECK(be, be->loop_amount > 0, brk, "gg used outside of a loop");
    const char* end = be->loops[be->loop_amount - 1].end_label;
    be_emitf(be, "JMP %s\n", end);
    return OK;
}

static err_t be_emit_if_chain_(backend_t* be, const ast_node_t* ifn)
{
    // IF children: cond, then, [tail]
    const ast_node_t* cond = ifn->left;
    const ast_node_t* then_st = cond ? cond->right : NULL;
    const ast_node_t* tail = then_st ? then_st->right : NULL;

    BE_CHECK(be, cond && then_st, ifn, "Internal: IF missing children");

    char* L_end = be_new_label(be, "if_end", ":L_");
    if (!L_end) return ERR_ALLOC;

    const ast_node_t* cur_if_cond = cond;
    const ast_node_t* cur_then    = then_st;
    const ast_node_t* cur_tail    = tail;

    while (1)
    {
        char* L_next = be_new_label(be, "if_next", ":L_");
        if (!L_next) { free(L_end); return ERR_ALLOC; }

        err_t rc = be_emit_cond_jfalse_(be, cur_if_cond, L_next);
        if (rc != OK) { free(L_next); free(L_end); return rc; }

        // then
        rc = be_emit_stmt_(be, cur_then);
        if (rc != OK) { free(L_next); free(L_end); return rc; }

        be_emitf(be, "JMP %s\n", L_end);
        be_emitf(be, "%s\n", L_next);
        free(L_next);

        if (!cur_tail)
            break;

        if (cur_tail->kind == ASTK_ELSE)
        {
            // ELSE child: body at else->left
            const ast_node_t* else_body = cur_tail->left;
            BE_CHECK(be, else_body != NULL, cur_tail, "Internal: ELSE missing body");
            rc = be_emit_stmt_(be, else_body);
            if (rc != OK) { free(L_end); return rc; }
            break;
        }

        BE_CHECK(be, cur_tail->kind == ASTK_BRANCH, cur_tail, "Internal: IF tail is not BRANCH/ELSE");

        // BRANCH children: cond, stmt, [tail]
        const ast_node_t* bc = cur_tail->left;
        const ast_node_t* bs = bc ? bc->right : NULL;
        const ast_node_t* bt = bs ? bs->right : NULL;

        BE_CHECK(be, bc && bs, cur_tail, "Internal: BRANCH missing (cond, stmt)");

        cur_if_cond = bc;
        cur_then    = bs;
        cur_tail    = bt;
    }

    be_emitf(be, "%s\n", L_end);
    free(L_end);
    return OK;
}

static err_t be_emit_return_(backend_t* be, const ast_node_t* r)
{
    const ast_node_t* expr = r->left;
    const ast_type_t ret_type = be->cur_fn ? be->cur_fn->ret_type : AST_TYPE_VOID;

    if (ast_type_is_void(ret_type))
    {
        be_emitf(be, "JMP %s\n", be->fn_end_label);
        return OK;
    }

    BE_CHECK(be, be_type_is_stack_scalar_(ret_type), r,
             "Unsupported return type '%s'", ast_type_to_cstr(ret_type));

    if (expr)
    {
        ast_type_t et = AST_TYPE_UNKNOWN;
        err_t rc = be_emit_expr_(be, expr, &et);
        if (rc != OK) return rc;

        BE_CHECK(be, be_type_has_value_(et), expr, "Return expression has no value");
        be_emit_cast_top_(be, et, ret_type);

        if (ret_type == AST_TYPE_FLOAT)
            be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_RET_F);
        else
            be_emitf(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }
    else
    {
        be_emitf(be, "PUSH 0\n");
        if (ret_type == AST_TYPE_FLOAT)
        {
            be_emitf(be, "ITOF\n");
            be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_RET_F);
        }
        else
            be_emitf(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }

    be_emitf(be, "JMP %s\n", be->fn_end_label);
    return OK;
}

static err_t be_emit_vdecl_(backend_t* be, const ast_node_t* vd)
{
    // VAR_DECL payload: name_id + type; optional init is first child
    const size_t name_id = vd->u.vdecl.name_id;
    const ast_type_t t   = vd->u.vdecl.type;

    BE_CHECK(be, be_type_is_stack_scalar_(t), vd,
             "Unsupported variable type '%s'", ast_type_to_cstr(t));

    const size_t off = be->next_local_offset++;
    err_t rc = be_bind_push(be, name_id, t, off, be->scope_depth);
    if (rc != OK) return rc;

    const ast_node_t* init = vd->left;
    if (init)
    {
        ast_type_t it = AST_TYPE_UNKNOWN;
        rc = be_emit_expr_(be, init, &it);
        if (rc != OK) return rc;

        BE_CHECK(be, be_type_has_value_(it), init, "Initializer has no value");
        be_emit_cast_top_(be, it, t);

        be_emit_store_bp_off_(be, off);
    }
    else
    {
        // default-init 0
        be_emitf(be, "PUSH 0\n");
        be_emit_store_bp_off_(be, off);
    }

    return OK;
}

static err_t be_emit_assign_(backend_t* be, const ast_node_t* asn)
{
    // ASSIGN payload: name_id; child[0] is expr
    const size_t name_id = asn->u.assign.name_id;
    const ast_node_t* rhs = asn->left;

    BE_CHECK(be, rhs != NULL, asn, "Assignment missing RHS");

    ssize_t bi = be_bind_lookup(be, name_id);
    BE_CHECK(be, bi >= 0, asn, "Assignment to unknown '%s'", ast_name_cstr(be->tree, name_id));

    ast_type_t rt = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, rhs, &rt);
    if (rc != OK) return rc;

    const binding_t* b = &be->binds[(size_t)bi];

    BE_CHECK(be, be_type_has_value_(rt), rhs, "Assignment RHS has no value");
    be_emit_cast_top_(be, rt, b->type);

    be_emit_store_bp_off_(be, b->offset);
    return OK;
}

static err_t be_emit_call_stmt_(backend_t* be, const ast_node_t* cs)
{
    const ast_node_t* call = cs->left;
    BE_CHECK(be, call && call->kind == ASTK_CALL, cs, "call-stmt missing call node");
    ast_type_t tmp = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, call, &tmp);
    if (rc != OK) return rc;

    if (!ast_type_is_void(tmp))
        be_emitf(be, "POP\n");
    return OK;
}

static err_t be_emit_expr_stmt_(backend_t* be, const ast_node_t* es)
{
    const ast_node_t* e = es->left;
    BE_CHECK(be, e != NULL, es, "expr-stmt missing expression");
    ast_type_t t = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, e, &t);
    if (rc != OK) return rc;
    if (!ast_type_is_void(t))
        be_emitf(be, "POP\n");
    return OK;
}

static err_t be_emit_print_(backend_t* be, const ast_node_t* pr)
{
    const ast_node_t* e = pr->left;
    BE_CHECK(be, e != NULL, pr, "print missing expression");

    ast_type_t t = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, e, &t);
    if (rc != OK) return rc;

    if (pr->kind == ASTK_FCOUT)
    {
        if (!be_type_is_float_(t)) be_emitf(be, "ITOF\n");
        be_emitf(be, "FTOPOUT\nPOP\n");
    }
    else
    {
        if (be_type_is_float_(t)) be_emitf(be, "FTOI\n");
        be_emitf(be, "TOPOUT\nPOP\n");
    }

    return OK;
}

static err_t be_emit_cmp_to_bool_(backend_t* be, const ast_node_t* op_node, token_kind_t opk)
{
    char* L_true = be_new_label(be, "cmp_true", ":L_");
    char* L_end  = be_new_label(be, "cmp_end", ":L_");
    if (!L_true || !L_end) { free(L_true); free(L_end); return ERR_ALLOC; }

    const char* jmp = NULL;
    switch (opk)
    {
        case TOK_OP_EQ:  jmp = "JE";  break;
        case TOK_OP_NEQ: jmp = "JNE"; break;
        case TOK_OP_LT:  jmp = "JB";  break;
        case TOK_OP_LTE: jmp = "JBE"; break;
        case TOK_OP_GT:  jmp = "JA";  break;
        case TOK_OP_GTE: jmp = "JAE"; break;
        default:
            free(L_true); free(L_end);
            BE_FAIL_NODE(be, op_node, "Unsupported compare operator");
    }

    be_emitf(be, "%s %s\n", jmp, L_true);
    be_emitf(be, "PUSH 0\nJMP %s\n", L_end);
    be_emitf(be, "%s\nPUSH 1\n", L_true);
    be_emitf(be, "%s\n", L_end);

    free(L_true);
    free(L_end);
    return OK;
}


static ast_type_t be_infer_expr_type_(const backend_t* be, const ast_node_t* e)
{
    if (!e) return AST_TYPE_UNKNOWN;

    if (e->type != AST_TYPE_UNKNOWN) return e->type;

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
            if (!name) return AST_TYPE_UNKNOWN;

            if (be_streq(name, "in")     || be_streq(name, "cap") ||
                be_streq(name, "cin")    || be_streq(name, "stinky"))
                return AST_TYPE_INT;

            if (be_streq(name, "fin")    || be_streq(name, "nocap"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "out") || be_streq(name, "pookie") ||
                be_streq(name, "cout") || be_streq(name, "menace"))
                return AST_TYPE_INT;

            if (be_streq(name, "fout") || be_streq(name, "rizz"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "draw") || be_streq(name, "clean_vm") ||
                be_streq(name, "gyat") || be_streq(name, "skibidi") ||
                be_streq(name, "set_pixel"))
                return AST_TYPE_VOID;

            if (be_streq(name, "sqrt") || be_streq(name, "pow") || be_streq(name, "mpow"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "xor") || be_streq(name, "shl") || be_streq(name, "shr"))
                return AST_TYPE_INT;

            const func_meta_t* fm = be_find_func(be, e->u.call.name_id);
            return fm ? fm->ret_type : AST_TYPE_UNKNOWN;
        }

        case ASTK_BUILTIN_UNARY:
            return (e->u.builtin_unary.id == AST_BUILTIN_FTOI) ? AST_TYPE_INT : AST_TYPE_FLOAT;

        case ASTK_UNARY:
        {
            const token_kind_t op = e->u.unary.op;
            if (op == TOK_OP_NOT) return AST_TYPE_INT;
                return be_infer_expr_type_(be, e->left);
        }

        case ASTK_BINARY:
        {
            const token_kind_t op = e->u.binary.op;

            if (is_bool_op_(op) || op == TOK_OP_AND || op == TOK_OP_OR)
                return AST_TYPE_INT;

            const ast_type_t lt = be_infer_expr_type_(be, e->left);
            const ast_type_t rt = be_infer_expr_type_(be, e->right);

            if (op == TOK_OP_POW)
                return (lt == AST_TYPE_INT && rt == AST_TYPE_INT) ? AST_TYPE_INT : AST_TYPE_FLOAT;

            if (be_type_is_float_(lt) || be_type_is_float_(rt)) return AST_TYPE_FLOAT;
            if (lt == AST_TYPE_UNKNOWN || rt == AST_TYPE_UNKNOWN) return AST_TYPE_UNKNOWN;

            return AST_TYPE_INT;
        }

        default:
            return AST_TYPE_UNKNOWN;
    }
}

static err_t be_emit_fcmp_res_to_bool_(backend_t* be, const ast_node_t* op_node, token_kind_t opk)
{
    char* L_true = be_new_label(be, "fcmp_true", ":L_");
    char* L_end  = be_new_label(be, "fcmp_end", ":L_");
    if (!L_true || !L_end) { free(L_true); free(L_end); return ERR_ALLOC; }

    const char* jmp = NULL;
    long long   k   = 0;
    switch (opk)
    {
        case TOK_OP_EQ:  jmp = "JE";  k = 0;  break;
        case TOK_OP_NEQ: jmp = "JNE"; k = 0;  break;
        case TOK_OP_LT:  jmp = "JE";  k = -1; break;
        case TOK_OP_LTE: jmp = "JNE"; k = 1;  break;
        case TOK_OP_GT:  jmp = "JE";  k = 1;  break;
        case TOK_OP_GTE: jmp = "JNE"; k = -1; break;
        default:
            free(L_true); free(L_end);
            BE_FAIL_NODE(be, op_node, "Unsupported float-compare operator");
    }

    be_emitf(be, "PUSH %lld\n", k);
    be_emitf(be, "%s %s\n", jmp, L_true);

    be_emitf(be, "PUSH 0\nJMP %s\n", L_end);
    be_emitf(be, "%s\nPUSH 1\n", L_true);
    be_emitf(be, "%s\n", L_end);

    free(L_true);
    free(L_end);
    return OK;
}

static err_t be_emit_expr_(backend_t* be, const ast_node_t* e, ast_type_t* out_type)
{
    if (!e) return ERR_BAD_ARG;

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
            if (e->u.num.lit_type == LIT_FLOAT)
                be_emitf(be, "PUSH %lf\n", e->u.num.lit.f64);
            else
                be_emitf(be, "PUSH %lld\n", (long long)e->u.num.lit.i64);
            if (out_type) *out_type = (e->type != AST_TYPE_UNKNOWN) ? e->type : be_literal_type_(e);
            return OK;

        case ASTK_IDENT:
        {
            ssize_t bi = be_bind_lookup(be, e->u.ident.name_id);
            BE_CHECK(be, bi >= 0, e, "Unknown identifier '%s'", ast_name_cstr(be->tree, e->u.ident.name_id));
            const binding_t* b = &be->binds[(size_t)bi];
            be_emit_load_bp_off_(be, b->offset);
            if (out_type) *out_type = b->type;
            return OK;
        }

        case ASTK_CALL:
        {
            err_t brc = be_emit_builtin_call_(be, e, out_type);
            if (brc == OK) return OK;
            if (brc != ERR_BAD_ARG) return brc;

            // child[0] = ARG_LIST
            const func_meta_t* fm = be_find_func(be, e->u.call.name_id);
            BE_CHECK(be, fm != NULL, e, "Call to unknown function '%s'", ast_name_cstr(be->tree, e->u.call.name_id));

            const ast_node_t* args = e->left;
            BE_CHECK(be, args && args->kind == ASTK_ARG_LIST, e, "Internal: CALL missing ARG_LIST");

            const size_t argc = arg_count_(args);
            BE_CHECK(be, argc == fm->param_count, e,
                     "Function '%s' expects %zu args, got %zu",
                     ast_name_cstr(be->tree, e->u.call.name_id),
                     fm->param_count, argc);

            // store args into RAM[SP + i]
            size_t i = 1;
            for (const ast_node_t* a = args->left; a; a = a->right, ++i)
            {
                ast_type_t at = AST_TYPE_UNKNOWN;
                err_t rc = be_emit_expr_(be, a, &at);
                if (rc != OK) return rc;

                if (i - 1 < fm->param_count)
                {
                    ast_type_t pt = fm->param_types[i - 1];
                    be_emit_cast_top_(be, at, pt);
                }

                be_emit_addr_sp_plus_(be, i);
                be_emitf(be, "POPM x%u\n", (unsigned)REG_TMPA); // pop arg into RAM[SP+i]
            }

            be_emitf(be, "CALL %s\n", fm->label);

            if (fm->ret_type == AST_TYPE_FLOAT)
                be_emitf(be, "FPUSHR fx%u\n", (unsigned)REG_RET_F);
            else if (ast_type_is_integer_like(fm->ret_type))
                be_emitf(be, "PUSHR x%u\n", (unsigned)REG_RET_I);

            if (out_type) *out_type = fm->ret_type;
            return OK;
        }

        case ASTK_UNARY:
        {
            const token_kind_t opk = e->u.unary.op;
            const ast_node_t* sub  = e->left;
            BE_CHECK(be, sub != NULL, e, "Unary missing operand");

            ast_type_t st = AST_TYPE_UNKNOWN;
            err_t rc = be_emit_expr_(be, sub, &st);
            if (rc != OK) return rc;

            if (opk == TOK_OP_PLUS)
            {
                if (out_type) *out_type = st;
                return OK;
            }

            if (opk == TOK_OP_MINUS)
            {
                // -x => 0 - x. Keep float temporaries in float scratch register.
                if (st == AST_TYPE_FLOAT)
                {
                    be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_TMP_F);
                    be_emitf(be, "PUSH 0\nITOF\n");
                    be_emitf(be, "FPUSHR fx%u\n", (unsigned)REG_TMP_F);
                    be_emitf(be, "FSUB\n");
                }
                else
                {
                    be_emitf(be, "POPR  x%u\n", (unsigned)REG_TMPA);
                    be_emitf(be, "PUSH 0\n");
                    be_emitf(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
                    be_emitf(be, "SUB\n");
                }
                if (out_type) *out_type = st;
                return OK;
            }

            if (opk == TOK_OP_NOT)
            {
                // logical not: (x == 0) ? 1 : 0
                if (be_type_is_float_(st)) be_emitf(be, "FTOI\n");
                be_emitf(be, "PUSH 0\n");
                // stack [x,0]
                char* L_true = be_new_label(be, "not_true", ":L_");
                char* L_end  = be_new_label(be, "not_end", ":L_");
                if (!L_true || !L_end) { free(L_true); free(L_end); return ERR_ALLOC; }

                be_emitf(be, "JE %s\n", L_true);
                be_emitf(be, "PUSH 0\nJMP %s\n", L_end);
                be_emitf(be, "%s\nPUSH 1\n", L_true);
                be_emitf(be, "%s\n", L_end);

                free(L_true);
                free(L_end);

                if (out_type) *out_type = AST_TYPE_INT;
                return OK;
            }

            BE_FAIL_NODE(be, e, "Unsupported unary operator");
        }

        case ASTK_BUILTIN_UNARY:
        {
            const ast_node_t* sub = e->left;
            BE_CHECK(be, sub != NULL, e, "builtin-unary missing operand");

            ast_type_t st = AST_TYPE_UNKNOWN;
            err_t rc = be_emit_expr_(be, sub, &st);
            if (rc != OK) return rc;

            switch (e->u.builtin_unary.id)
            {
                case AST_BUILTIN_FLOOR:
                    if (!be_type_is_float_(st)) be_emitf(be, "ITOF\n");
                    be_emitf(be, "FLOOR\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_CEIL:
                    if (!be_type_is_float_(st)) be_emitf(be, "ITOF\n");
                    be_emitf(be, "CEIL\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_ROUND:
                    if (!be_type_is_float_(st)) be_emitf(be, "ITOF\n");
                    be_emitf(be, "ROUND\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_ITOF:
                    if (!be_type_is_float_(st)) be_emitf(be, "ITOF\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_FTOI:
                    if (be_type_is_float_(st)) be_emitf(be, "FTOI\n");
                    if (out_type) *out_type = AST_TYPE_INT;
                    return OK;

                default: BE_FAIL_NODE(be, e, "Unknown builtin-unary id");
            }
        }

        case ASTK_BINARY:
        {
            const token_kind_t opk = e->u.binary.op;
            const ast_node_t* a = e->left;
            const ast_node_t* b = a ? a->right : NULL;
            BE_CHECK(be, a && b, e, "Binary missing operands");

            if (opk == TOK_OP_AND || opk == TOK_OP_OR)
            {
                ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
                err_t rc = be_emit_expr_(be, a, &at);
                if (rc != OK) return rc;
                if (be_type_is_float_(at)) be_emitf(be, "FTOI\n");

                rc = be_emit_expr_(be, b, &bt);
                if (rc != OK) return rc;
                if (be_type_is_float_(bt)) be_emitf(be, "FTOI\n");

                be_emitf(be, (opk == TOK_OP_AND) ? "AND\n" : "OR\n");
                if (out_type) *out_type = AST_TYPE_INT;
                return OK;
            }

            if (is_bool_op_(opk))
            {   
                ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
                err_t rc = be_emit_expr_(be, a, &at);
                if (rc != OK) return rc;
                rc = be_emit_expr_(be, b, &bt);
                if (rc != OK) return rc;

                if (be_type_is_float_(at) || be_type_is_float_(bt))
                {
                    if (!be_type_is_float_(bt))
                    {
                        be_emitf(be, "ITOF\n");
                        bt = AST_TYPE_FLOAT;
                    }
                    if (!be_type_is_float_(at))
                    {
                        be_emitf(be, "FPOPR fx%u\n", (unsigned)REG_TMP_F);
                        be_emitf(be, "ITOF\n");
                        be_emitf(be, "FPUSHR fx%u\n", (unsigned)REG_TMP_F);
                        at = AST_TYPE_FLOAT;
                    }

                    be_emitf(be, "FCMP\n");
                    rc = be_emit_fcmp_res_to_bool_(be, e, opk);
                }
                else
                    rc = be_emit_cmp_to_bool_(be, e, opk);

                if (rc != OK) return rc;
                if (out_type) *out_type = AST_TYPE_INT;
                return OK;
            }

            if (opk == TOK_OP_POW)
            {
                ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
                err_t rc = be_emit_expr_(be, a, &at);
                if (rc != OK) return rc;
                rc = be_emit_expr_(be, b, &bt);
                if (rc != OK) return rc;

                if      (at == AST_TYPE_INT   && bt == AST_TYPE_INT)   be_emitf(be, "POW\n");
                else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_INT)   be_emitf(be, "FPOW\n");
                else if (at == AST_TYPE_INT   && bt == AST_TYPE_FLOAT) be_emitf(be, "POWF\n");
                else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_FLOAT) be_emitf(be, "FPOWF\n");
                else BE_FAIL_NODE(be, e, "Unsupported types for ^ (need int/float operands)");

                if (out_type) *out_type = (at == AST_TYPE_INT && bt == AST_TYPE_INT) ? AST_TYPE_INT : AST_TYPE_FLOAT;
                return OK;
            }

            ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
            const ast_type_t ta = be_infer_expr_type_(be, a);
            const ast_type_t tb = be_infer_expr_type_(be, b);
            const int want_float = (be_type_is_float_(ta) || be_type_is_float_(tb));

            err_t rc = be_emit_expr_(be, a, &at);
            if (rc != OK) return rc;

            if (want_float)
            {
                if (!be_type_is_float_(at)) be_emitf(be, "ITOF\n");
                at = AST_TYPE_FLOAT;
            }
            else
            {
                if (be_type_is_float_(at)) be_emitf(be, "FTOI\n");
                at = AST_TYPE_INT;
            }

            rc = be_emit_expr_(be, b, &bt);
            if (rc != OK) return rc;

            if (want_float)
            {
                if (!be_type_is_float_(bt)) be_emitf(be, "ITOF\n");
                bt = AST_TYPE_FLOAT;
            }
            else
            {
                if (be_type_is_float_(bt)) be_emitf(be, "FTOI\n");
                bt = AST_TYPE_INT;
            }

            if (opk == TOK_OP_PLUS)       be_emitf(be, want_float ? "FADD\n" : "ADD\n");
            else if (opk == TOK_OP_MINUS) be_emitf(be, want_float ? "FSUB\n" : "SUB\n");
            else if (opk == TOK_OP_MUL)   be_emitf(be, want_float ? "FMUL\n" : "MUL\n");
            else if (opk == TOK_OP_DIV)   be_emitf(be, want_float ? "FDIV\n" : "DIV\n");
            else
                BE_FAIL_NODE(be, e, "Unsupported binary operator");

            if (out_type) *out_type = want_float ? AST_TYPE_FLOAT : AST_TYPE_INT;
            return OK;
        }

        default:
            BE_FAIL_NODE(be, e, "Backend: unsupported expr kind %s", ast_kind_to_cstr(e->kind));
    }
}

