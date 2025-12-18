#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void be_set_error_(backend_t* be, token_pos_t pos, size_t fallback_offset,
                          const char* fmt, ...)
{
    if (!be || !be->op) return;

    be->op->error_pos = (pos.offset != 0 ? pos.offset : fallback_offset);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(be->op->error_msg, sizeof(be->op->error_msg), fmt, ap);
    va_end(ap);

    {
        size_t len = strlen(be->op->error_msg);
        snprintf(be->op->error_msg + len,
                 sizeof(be->op->error_msg) - len,
                 " at %zu:%zu (offset: %zu)",
                 pos.line, pos.column, be->op->error_pos);
    }
}

#define BE_FAIL_NODE(be, nodeptr, fmt, ...)                                   \
    block_begin                                                               \
        const ast_node_t* __be_node = (const ast_node_t*)(nodeptr);           \
        token_pos_t __be_pos = __be_node ? __be_node->pos : (token_pos_t){0}; \
        be_set_error_((be), __be_pos, __be_pos.offset, (fmt), ##__VA_ARGS__); \
        return ERR_SYNTAX;                                                    \
    block_end

#define BE_CHECK(be, cond, node, fmt, ...)                    \
    block_begin                                               \
        if (!(cond)) {                                        \
            BE_FAIL_NODE((be), (node), (fmt), ##__VA_ARGS__); \
        }                                                     \
    block_end

static int be_emitf_(backend_t* be, const char* fmt, ...)
{
    if (!be || !be->op->out_file) return 0;
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(be->op->out_file, fmt, ap);
    va_end(ap);
    return r;
}

static char* be_strdup_printf_(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (need < 0) return NULL;

    char* s = (char*)calloc((size_t)need + 1, 1);
    if (!s) return NULL;

    va_start(ap, fmt);
    vsnprintf(s, (size_t)need + 1, fmt, ap);
    va_end(ap);
    return s;
}

static char* be_new_label_(backend_t* be, const char* prefix)
{
    if (!be) return NULL;
    return be_strdup_printf_(":L_%s_%zu", prefix ? prefix : "x", be->label_counter++);
}

#define VEC_GROW(ptr, cap, want, type)                         \
    block_begin                                                \
        if ((want) > (cap)) {                                  \
            size_t new_cap = (cap) ? (cap) * 2 : 8;            \
            while (new_cap < (want)) new_cap *= 2;             \
            void* np = realloc((ptr), new_cap * sizeof(type)); \
            if (!np) return ERR_ALLOC;                         \
            (ptr) = (type*)np;                                 \
            (cap) = new_cap;                                   \
        }                                                      \
    block_end

static int streq_(const char* a, const char* b) { return (a && b && strcmp(a,b)==0); }

static err_t              be_collect_funcs_(backend_t* be, const ast_node_t* program);
static const func_meta_t* be_find_func_    (const backend_t* be, size_t name_id);

static err_t be_emit_program_(backend_t* be, const ast_node_t* program);

static err_t be_emit_func_    (backend_t* be, const ast_node_t* fn);

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

static ssize_t be_bind_lookup_(const backend_t* be, size_t name_id)
{
    if (!be) return -1;
    for (ssize_t i = (ssize_t)be->bind_amount - 1; i >= 0; --i)
        if (be->binds[(size_t)i].name_id == name_id)
            return i;
    return -1;
}

static err_t be_bind_push_(backend_t* be, size_t name_id, ast_type_t type, size_t offset, size_t depth)
{
    VEC_GROW(be->binds, be->bind_cap, be->bind_amount + 1, binding_t);
    be->binds[be->bind_amount++] = (binding_t){
        .name_id = name_id, .type = type, .offset = offset, .depth = depth
    };
    return OK;
}

static void be_bind_pop_depth_(backend_t* be, size_t depth)
{
    if (!be) return;
    while (be->bind_amount > 0 && be->binds[be->bind_amount - 1].depth == depth)
        be->bind_amount--;
}

static void be_emit_addr_bp_off_(backend_t* be, size_t offset)
{
    // x13 = x15
    be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_BP);
    be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);

    // x13 = x13 + offset
    be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
    be_emitf_(be, "PUSH  %zu\n", offset);
    be_emitf_(be, "ADD\n");
    be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);
}

static void be_emit_load_bp_off_(backend_t* be, size_t offset)
{
    be_emit_addr_bp_off_(be, offset);
    be_emitf_(be, "PUSHM x%u\n", (unsigned)REG_TMPA);
}

static void be_emit_store_bp_off_(backend_t* be, size_t offset)
{
    be_emit_addr_bp_off_(be, offset);
    be_emitf_(be, "POPM x%u\n", (unsigned)REG_TMPA);
}

// Compute addr = SP + imm into x13
static void be_emit_addr_sp_plus_(backend_t* be, size_t imm)
{
    be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_SP);
    be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);

    if (imm != 0)
    {
        be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
        be_emitf_(be, "PUSH  %zu\n", imm);
        be_emitf_(be, "ADD\n");
        be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);
    }
}

static void be_count_locals_rec_(const ast_node_t* n, size_t* out)
{
    if (!n || !out) return;

    if (n->kind == ASTK_VAR_DECL)
        (*out)++;

    for (const ast_node_t* c = n->left; c; c = c->right)
        be_count_locals_rec_(c, out);
}

static const func_meta_t* be_find_func_(const backend_t* be, size_t name_id)
{
    if (!be) return NULL;
    for (size_t i = 0; i < be->func_amount; ++i)
        if (be->funcs[i].name_id == name_id)
            return &be->funcs[i];
    return NULL;
}

static err_t be_collect_funcs_(backend_t* be, const ast_node_t* program)
{
    if (!be || !program) return ERR_BAD_ARG;

    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        BE_CHECK(be, fn->kind == ASTK_FUNC, fn, "Internal: PROGRAM child is not FUNC");

        size_t name_id = fn->u.func.name_id;
        BE_CHECK(be, be_find_func_(be, name_id) == NULL, fn,
                 "Duplicate function '%s'", ast_name_cstr(be->tree, name_id));

        const ast_node_t* plist = fn->left;
        BE_CHECK(be, plist && plist->kind == ASTK_PARAM_LIST, fn, "Internal: FUNC missing PARAM_LIST");

        size_t pcount = 0;
        for (const ast_node_t* p = plist->left; p; p = p->right) pcount++;

        ast_type_t* ptypes = NULL;
        if (pcount)
        {
            ptypes = (ast_type_t*)calloc(pcount, sizeof(ast_type_t));
            if (!ptypes) return ERR_ALLOC;

            size_t i = 0;
            for (const ast_node_t* p = plist->left; p; p = p->right)
            {
                BE_CHECK(be, p->kind == ASTK_PARAM, p, "Internal: PARAM_LIST child not PARAM");
                ptypes[i++] = p->u.param.type;
            }
        }

        size_t locals = 0;
        const ast_node_t* body = plist->right;
        if (!body) body = fn->left ? fn->left->right : NULL;
        if (body) be_count_locals_rec_(body, &locals);

        char* label = be_strdup_printf_(":fn_%s", ast_name_cstr(be->tree, name_id));
        if (!label) { free(ptypes); return ERR_ALLOC; }

        VEC_GROW(be->funcs, be->func_cap, be->func_amount + 1, func_meta_t);
        be->funcs[be->func_amount++] = (func_meta_t){
            .name_id = name_id,
            .label = label,
            .ret_type = fn->u.func.ret_type,
            .param_count = pcount,
            .param_types = ptypes,
            .local_count = locals
        };
    }

    return OK;
}

err_t backend_emit_asm(const ast_tree_t* tree, operational_data_t* op_data)
{
    if (!tree || !tree->root || !op_data) return ERR_BAD_ARG;

    backend_t be = { 0 };
    be.tree = tree;
    be.op   = op_data;

    const ast_node_t* program = tree->root;
    if (program->kind != ASTK_PROGRAM)
        be_set_error_(&be, program->pos, program->pos.offset, "Root is not PROGRAM");

    err_t rc = be_collect_funcs_(&be, program);
    if (rc != OK) goto cleanup;

    {
        size_t main_id = SIZE_MAX;
        for (size_t i = 0; i < tree->nametable.amount; ++i)
            if (tree->nametable.data[i].name && strcmp(tree->nametable.data[i].name, "main") == 0)
                { main_id = i; break; }

        if (main_id == SIZE_MAX || !be_find_func_(&be, main_id))
        {
            be_set_error_(&be, (token_pos_t){1,1,0}, 0, "No function 'main' found");
            rc = ERR_SYNTAX;
            goto cleanup;
        }
    }

    rc = be_emit_program_(&be, program);

cleanup:
    for (size_t i = 0; i < be.func_amount; ++i)
    {
        free(be.funcs[i].label);
        free(be.funcs[i].param_types);
    }
    free(be.funcs);
    free(be.binds);

    for (size_t i = 0; i < be.loop_amount; ++i)
        free(be.loops[i].end_label);
    free(be.loops);

    free(be.fn_end_label);

    return rc;
}

static err_t be_emit_program_(backend_t* be, const ast_node_t* program)
{
    // init SP/BP = 0; CALL main; HLT
    be_emitf_(be, "; --- program entry ---\n");
    be_emitf_(be, "PUSH 0\nPOPR x%u\n", (unsigned)REG_SP);
    be_emitf_(be, "PUSH 0\nPOPR x%u\n", (unsigned)REG_BP);

    // CALL :fn_main
    {
        size_t main_id = SIZE_MAX;
        for (size_t i = 0; i < be->tree->nametable.amount; ++i)
            if (be->tree->nametable.data[i].name && strcmp(be->tree->nametable.data[i].name, "main") == 0)
                { main_id = i; break; }

        const func_meta_t* fm = be_find_func_(be, main_id);
        BE_CHECK(be, fm != NULL, program, "No main() metadata");
        be_emitf_(be, "CALL %s\n", fm->label);
    }

    be_emitf_(be, "HLT\n\n");

    // emit all functions
    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        err_t rc = be_emit_func_(be, fn);
        if (rc != OK) return rc;
        be_emitf_(be, "\n");
    }

    return OK;
}

static err_t be_emit_func_(backend_t* be, const ast_node_t* fn)
{
    BE_CHECK(be, fn && fn->kind == ASTK_FUNC, fn, "Internal: expected FUNC");
    const func_meta_t* meta = be_find_func_(be, fn->u.func.name_id);
    BE_CHECK(be, meta != NULL, fn, "Internal: no metadata for function '%s'",
             ast_name_cstr(be->tree, fn->u.func.name_id));

    be->cur_fn = meta;

    free(be->fn_end_label);
    be->fn_end_label = be_new_label_(be, "fn_end");
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
            err_t rc = be_bind_push_(be, p->u.param.name_id, p->u.param.type, 1 + i, be->scope_depth);
            if (rc != OK) return rc;
            i++;
        }
    }

    be_emitf_(be, "; --- function %s ---\n", ast_name_cstr(be->tree, meta->name_id));
    be_emitf_(be, "%s\n", meta->label);

    //   RAM[SP] = oldBP
    //   BP = SP
    //   SP = SP + (1 + param_count + local_count)
    //
    // Using: x13 as addr temp, x14=SP, x15=BP
    be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_BP);        // push old BP
    be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_SP);        // copy SP into x13
    be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);
    be_emitf_(be, "POPM  x%u\n", (unsigned)REG_TMPA);      // RAM[SP] = oldBP

    be_emitf_(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_SP, (unsigned)REG_BP); // BP = SP

    const size_t frame = 1 + meta->param_count + meta->local_count;
    be_emitf_(be, "PUSHR x%u\nPUSH %zu\nADD\nPOPR x%u\n",
              (unsigned)REG_SP, frame, (unsigned)REG_SP);

    const ast_node_t* plist = fn->left;
    const ast_node_t* body  = plist ? plist->right : NULL;
    if (!body) body = fn->left ? fn->left->right : NULL;

    BE_CHECK(be, body != NULL, fn, "Function has no body");
    err_t rc = be_emit_stmt_(be, body);
    if (rc != OK) return rc;

    if (meta->ret_type != AST_TYPE_VOID)
    {
        be_emitf_(be, "; implicit return 0 (defensive)\n");
        be_emitf_(be, "PUSH 0\n");
        be_emitf_(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }

    be_emitf_(be, "%s\n", be->fn_end_label);

    // SP = BP
    be_emitf_(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_BP, (unsigned)REG_SP);

    // BP = RAM[BP]
    be_emitf_(be, "PUSHR x%u\nPOPR x%u\n", (unsigned)REG_BP, (unsigned)REG_TMPA);
    be_emitf_(be, "PUSHM x%u\n", (unsigned)REG_TMPA);
    be_emitf_(be, "POPR  x%u\n", (unsigned)REG_BP);

    be_emitf_(be, "RET\n");

    return OK;
}

#define BE_BUILTIN0_RET(BNAME, INSTR, RETTYPE)                      \
    block_begin                                                     \
        if (streq_(name, BNAME)) {                                  \
            BE_CHECK(be, argc == 0, call, BNAME "() takes 0 args"); \
            be_emitf_(be, INSTR "\n");                              \
            if (out_type) *out_type = (RETTYPE);                    \
            return OK;                                              \
        }                                                           \
    block_end

#define BE_BUILTIN0_VOID(BNAME, INSTR)                              \
    block_begin                                                     \
        if (streq_(name, BNAME)) {                                  \
            BE_CHECK(be, argc == 0, call, BNAME "() takes 0 args"); \
            be_emitf_(be, INSTR "\n");                              \
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

    if (streq_(name, "out")  || streq_(name, "fout") || streq_(name, "cout") ||
        streq_(name, "pookie") || streq_(name, "rizz") || streq_(name, "menace"))
    {
        BE_CHECK(be, argc == 1, call, "%s() takes 1 arg", name);

        ast_type_t at = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &at) == OK, call, "bad arg");

        const int is_fout = streq_(name, "fout") || streq_(name, "rizz");
        const int is_cout = streq_(name, "cout") || streq_(name, "menace");

        if (is_fout)
        {
            if (at != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
            be_emitf_(be, "FTOPOUT\n");
            ret_type = AST_TYPE_FLOAT;
        }
        else
        {
            if (at == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
            be_emitf_(be, is_cout ? "CTOPOUT\n" : "TOPOUT\n");
            ret_type = AST_TYPE_INT;
        }

        rc = OK;
    }
    else if (streq_(name, "set_pixel"))
    {
        BE_CHECK(be, argc == 3, call, "set_pixel(x,y,ch) takes 3 args");

        // addr = y*W + x
        ast_type_t ty = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 1), &ty) == OK, call, "bad y");
        if (ty == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
        be_emitf_(be, "PUSH %d\nMUL\n", (int)BE_SCREEN_WIDTH);

        ast_type_t tx = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 0), &tx) == OK, call, "bad x");
        if (tx == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
        be_emitf_(be, "ADD\nPOPR x%u\n", (unsigned)REG_TMPA); // x13 = addr

        ast_type_t tch = AST_TYPE_UNKNOWN;
        BE_CHECK(be, be_emit_expr_(be, arg_at_(args, 2), &tch) == OK, call, "bad ch");
        if (tch == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
        be_emitf_(be, "POPVM x%u\n", (unsigned)REG_TMPA);

        ret_type = AST_TYPE_VOID;
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

    be_bind_pop_depth_(be, depth);
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
    if (*bt != AST_TYPE_FLOAT)
    {
        be_emitf_(be, "ITOF\n");
        *bt = AST_TYPE_FLOAT;
    }

    if (*at != AST_TYPE_FLOAT)
    {
        be_emitf_(be, "FPOPR fx%u\n", (unsigned)REG_TMP_F);
        be_emitf_(be, "ITOF\n");
        be_emitf_(be, "FPUSHR fx%u\n", (unsigned)REG_TMP_F);
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
        if (at == AST_TYPE_FLOAT || bt == AST_TYPE_FLOAT)
        {
            be_promote_pair_to_float_(be, &at, &bt);
            be_emitf_(be, "FCMP\n");

            switch (opk)
            {
                case TOK_OP_EQ:  be_emitf_(be, "PUSH 0\nJNE %s\n", L_false); break;
                case TOK_OP_NEQ: be_emitf_(be, "PUSH 0\nJE  %s\n", L_false); break;
                case TOK_OP_LT:  be_emitf_(be, "PUSH -1\nJNE %s\n", L_false); break;
                case TOK_OP_LTE: be_emitf_(be, "PUSH 1\nJE  %s\n", L_false); break;
                case TOK_OP_GT:  be_emitf_(be, "PUSH 1\nJNE %s\n", L_false); break;
                case TOK_OP_GTE: be_emitf_(be, "PUSH -1\nJE  %s\n", L_false); break;
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

        be_emitf_(be, "%s %s\n", jfalse, L_false);
        return OK;
    }

    ast_type_t ct = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, cond, &ct);
    if (rc != OK) return rc;

    if (ct == AST_TYPE_FLOAT)
    {
        be_emitf_(be, "PUSH 0\nITOF\n"); // 0.0
        be_emitf_(be, "FCMP\n");         // compare cond vs 0.0 -> int
        be_emitf_(be, "PUSH 0\n");       // res == 0 ?
        be_emitf_(be, "JE %s\n", L_false);
    }
    else
    {
        be_emitf_(be, "PUSH 0\n");
        be_emitf_(be, "JE %s\n", L_false);
    }

    return OK;
}

static err_t be_emit_while_(backend_t* be, const ast_node_t* w)
{
    const ast_node_t* cond = w->left;
    const ast_node_t* body = cond ? cond->right : NULL;

    BE_CHECK(be, cond && body, w, "Internal: WHILE must have (cond, body)");

    char* L_begin = be_new_label_(be, "while_begin");
    char* L_end   = be_new_label_(be, "while_end");
    if (!L_begin || !L_end) { free(L_begin); free(L_end); return ERR_ALLOC; }

    // push loop ctx
    VEC_GROW(be->loops, be->loop_cap, be->loop_amount + 1, loop_ctx_t);
    be->loops[be->loop_amount++] = (loop_ctx_t){ .end_label = strdup(L_end) };

    be_emitf_(be, "%s\n", L_begin);

    err_t rc = be_emit_cond_jfalse_(be, cond, L_end);
    if (rc != OK) goto done;

    rc = be_emit_stmt_(be, body);
    if (rc != OK) goto done;

    be_emitf_(be, "JMP %s\n", L_begin);
    be_emitf_(be, "%s\n", L_end);

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
    be_emitf_(be, "JMP %s\n", end);
    return OK;
}

static err_t be_emit_if_chain_(backend_t* be, const ast_node_t* ifn)
{
    // IF children: cond, then, [tail]
    const ast_node_t* cond = ifn->left;
    const ast_node_t* then_st = cond ? cond->right : NULL;
    const ast_node_t* tail = then_st ? then_st->right : NULL;

    BE_CHECK(be, cond && then_st, ifn, "Internal: IF missing children");

    char* L_end = be_new_label_(be, "if_end");
    if (!L_end) return ERR_ALLOC;

    const ast_node_t* cur_if_cond = cond;
    const ast_node_t* cur_then    = then_st;
    const ast_node_t* cur_tail    = tail;

    while (1)
    {
        char* L_next = be_new_label_(be, "if_next");
        if (!L_next) { free(L_end); return ERR_ALLOC; }

        err_t rc = be_emit_cond_jfalse_(be, cur_if_cond, L_next);
        if (rc != OK) { free(L_next); free(L_end); return rc; }

        // then
        rc = be_emit_stmt_(be, cur_then);
        if (rc != OK) { free(L_next); free(L_end); return rc; }

        be_emitf_(be, "JMP %s\n", L_end);
        be_emitf_(be, "%s\n", L_next);
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

    be_emitf_(be, "%s\n", L_end);
    free(L_end);
    return OK;
}

static err_t be_emit_return_(backend_t* be, const ast_node_t* r)
{
    const ast_node_t* expr = r->left;

    if (be->cur_fn && be->cur_fn->ret_type == AST_TYPE_VOID)
    {
        // ignore optional expression
        be_emitf_(be, "JMP %s\n", be->fn_end_label);
        return OK;
    }

    if (expr)
    {
        ast_type_t et = AST_TYPE_UNKNOWN;
        err_t rc = be_emit_expr_(be, expr, &et);
        if (rc != OK) return rc;

        if (be->cur_fn && be->cur_fn->ret_type == AST_TYPE_FLOAT)
            be_emitf_(be, "FPOPR fx%u\n", (unsigned)REG_RET_F);
        else
            be_emitf_(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }
    else
    {
        // default 0
        be_emitf_(be, "PUSH 0\n");
        be_emitf_(be, "POPR x%u\n", (unsigned)REG_RET_I);
    }

    be_emitf_(be, "JMP %s\n", be->fn_end_label);
    return OK;
}

static err_t be_emit_vdecl_(backend_t* be, const ast_node_t* vd)
{
    // VAR_DECL payload: name_id + type; optional init is first child
    const size_t name_id = vd->u.vdecl.name_id;
    const ast_type_t t   = vd->u.vdecl.type;

    const size_t off = be->next_local_offset++;
    err_t rc = be_bind_push_(be, name_id, t, off, be->scope_depth);
    if (rc != OK) return rc;

    const ast_node_t* init = vd->left;
    if (init)
    {
        ast_type_t it = AST_TYPE_UNKNOWN;
        rc = be_emit_expr_(be, init, &it);
        if (rc != OK) return rc;

        // int->float if var is float
        if (t == AST_TYPE_FLOAT && it != AST_TYPE_FLOAT)
            be_emitf_(be, "ITOF\n");

        // float->int if var is int
        if (t != AST_TYPE_FLOAT && it == AST_TYPE_FLOAT)
            be_emitf_(be, "FTOI\n");

        be_emit_store_bp_off_(be, off);
    }
    else
    {
        // default-init 0
        be_emitf_(be, "PUSH 0\n");
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

    ssize_t bi = be_bind_lookup_(be, name_id);
    BE_CHECK(be, bi >= 0, asn, "Assignment to unknown '%s'", ast_name_cstr(be->tree, name_id));

    ast_type_t rt = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, rhs, &rt);
    if (rc != OK) return rc;

    const binding_t* b = &be->binds[(size_t)bi];

    if (b->type == AST_TYPE_FLOAT && rt != AST_TYPE_FLOAT)
        be_emitf_(be, "ITOF\n");
    if (b->type != AST_TYPE_FLOAT && rt == AST_TYPE_FLOAT)
        be_emitf_(be, "FTOI\n");

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

    if (tmp != AST_TYPE_VOID)
        be_emitf_(be, "POP\n");
    return OK;
}

static err_t be_emit_expr_stmt_(backend_t* be, const ast_node_t* es)
{
    const ast_node_t* e = es->left;
    BE_CHECK(be, e != NULL, es, "expr-stmt missing expression");
    ast_type_t t = AST_TYPE_UNKNOWN;
    err_t rc = be_emit_expr_(be, e, &t);
    if (rc != OK) return rc;
    if (t != AST_TYPE_VOID)
        be_emitf_(be, "POP\n");
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
        if (t != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
        be_emitf_(be, "FTOPOUT\nPOP\n");
    }
    else
    {
        if (t == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
        be_emitf_(be, "TOPOUT\nPOP\n");
    }

    return OK;
}

static err_t be_emit_cmp_to_bool_(backend_t* be, const ast_node_t* op_node, token_kind_t opk)
{
    char* L_true = be_new_label_(be, "cmp_true");
    char* L_end  = be_new_label_(be, "cmp_end");
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

    be_emitf_(be, "%s %s\n", jmp, L_true);
    be_emitf_(be, "PUSH 0\nJMP %s\n", L_end);
    be_emitf_(be, "%s\nPUSH 1\n", L_true);
    be_emitf_(be, "%s\n", L_end);

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
            ssize_t idx = be_bind_lookup_(be, e->u.ident.name_id);
            return (idx >= 0) ? be->binds[(size_t)idx].type : AST_TYPE_UNKNOWN;
        }

        case ASTK_CALL:
        {
            const char* name = ast_name_cstr(be->tree, e->u.call.name_id);
            if (!name) return AST_TYPE_UNKNOWN;

            if (streq_(name, "in")     || streq_(name, "cap") ||
                streq_(name, "cin")    || streq_(name, "stinky"))
                return AST_TYPE_INT;

            if (streq_(name, "fin")    || streq_(name, "nocap"))
                return AST_TYPE_FLOAT;

            const func_meta_t* fm = be_find_func_(be, e->u.call.name_id);
            return fm ? fm->ret_type : AST_TYPE_UNKNOWN;
        }

        case ASTK_BUILTIN_UNARY:
            return AST_TYPE_FLOAT;

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

            if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT) return AST_TYPE_FLOAT;
            if (lt == AST_TYPE_UNKNOWN || rt == AST_TYPE_UNKNOWN) return AST_TYPE_UNKNOWN;

            return AST_TYPE_INT;
        }

        default:
            return AST_TYPE_UNKNOWN;
    }
}

static err_t be_emit_fcmp_res_to_bool_(backend_t* be, const ast_node_t* op_node, token_kind_t opk)
{
    char* L_true = be_new_label_(be, "fcmp_true");
    char* L_end  = be_new_label_(be, "fcmp_end");
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

    be_emitf_(be, "PUSH %lld\n", k);
    be_emitf_(be, "%s %s\n", jmp, L_true);

    be_emitf_(be, "PUSH 0\nJMP %s\n", L_end);
    be_emitf_(be, "%s\nPUSH 1\n", L_true);
    be_emitf_(be, "%s\n", L_end);

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
                be_emitf_(be, "PUSH %lf\n", e->u.num.lit.f64);
            else
                be_emitf_(be, "PUSH %lld\n", (long long)e->u.num.lit.i64);
            if (out_type) *out_type = e->type;
            return OK;

        case ASTK_IDENT:
        {
            ssize_t bi = be_bind_lookup_(be, e->u.ident.name_id);
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
            const func_meta_t* fm = be_find_func_(be, e->u.call.name_id);
            BE_CHECK(be, fm != NULL, e, "Call to unknown function '%s'", ast_name_cstr(be->tree, e->u.call.name_id));

            const ast_node_t* args = e->left;
            BE_CHECK(be, args && args->kind == ASTK_ARG_LIST, e, "Internal: CALL missing ARG_LIST");

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
                    if (pt == AST_TYPE_FLOAT && at != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                    if (pt != AST_TYPE_FLOAT && at == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
                }

                be_emit_addr_sp_plus_(be, i);
                be_emitf_(be, "POPM x%u\n", (unsigned)REG_TMPA); // pop arg into RAM[SP+i]
            }

            be_emitf_(be, "CALL %s\n", fm->label);

            if (fm->ret_type == AST_TYPE_FLOAT)
                be_emitf_(be, "FPUSHR fx%u\n", (unsigned)REG_RET_F);
            else if (fm->ret_type == AST_TYPE_INT)
                be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_RET_I);

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
                // -x  => 0 x SUB
                be_emitf_(be, "POPR  x%u\n", (unsigned)REG_TMPA);
                be_emitf_(be, "PUSH 0\n");
                if (st == AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                be_emitf_(be, "PUSHR x%u\n", (unsigned)REG_TMPA);
                be_emitf_(be, (st == AST_TYPE_FLOAT) ? "FSUB\n" : "SUB\n");
                if (out_type) *out_type = st;
                return OK;
            }

            if (opk == TOK_OP_NOT)
            {
                // logical not: (x == 0) ? 1 : 0
                if (st == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
                be_emitf_(be, "PUSH 0\n");
                // stack [x,0]
                char* L_true = be_new_label_(be, "not_true");
                char* L_end  = be_new_label_(be, "not_end");
                if (!L_true || !L_end) { free(L_true); free(L_end); return ERR_ALLOC; }

                be_emitf_(be, "JE %s\n", L_true);
                be_emitf_(be, "PUSH 0\nJMP %s\n", L_end);
                be_emitf_(be, "%s\nPUSH 1\n", L_true);
                be_emitf_(be, "%s\n", L_end);

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
                    if (st != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                    be_emitf_(be, "FLOOR\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_CEIL:
                    if (st != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                    be_emitf_(be, "CEIL\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_ROUND:
                    if (st != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                    be_emitf_(be, "ROUND\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_ITOF:
                    if (st != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                    if (out_type) *out_type = AST_TYPE_FLOAT;
                    return OK;

                case AST_BUILTIN_FTOI:
                    if (st == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
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
                if (at == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");

                rc = be_emit_expr_(be, b, &bt);
                if (rc != OK) return rc;
                if (bt == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");

                be_emitf_(be, (opk == TOK_OP_AND) ? "AND\n" : "OR\n");
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

                if (at == AST_TYPE_FLOAT || bt == AST_TYPE_FLOAT)
                {
                    if (bt != AST_TYPE_FLOAT)
                    {
                        be_emitf_(be, "ITOF\n");
                        bt = AST_TYPE_FLOAT;
                    }
                    if (at != AST_TYPE_FLOAT)
                    {
                        be_emitf_(be, "FPOPR fx%u\n", (unsigned)REG_TMP_F);
                        be_emitf_(be, "ITOF\n");
                        be_emitf_(be, "FPUSHR fx%u\n", (unsigned)REG_TMP_F);
                        at = AST_TYPE_FLOAT;
                    }

                    be_emitf_(be, "FCMP\n");
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

                if      (at == AST_TYPE_INT   && bt == AST_TYPE_INT)   be_emitf_(be, "POW\n");
                else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_INT)   be_emitf_(be, "FPOW\n");
                else if (at == AST_TYPE_INT   && bt == AST_TYPE_FLOAT) be_emitf_(be, "POWF\n");
                else if (at == AST_TYPE_FLOAT && bt == AST_TYPE_FLOAT) be_emitf_(be, "FPOWF\n");
                else BE_FAIL_NODE(be, e, "Unsupported types for ^ (need int/float operands)");

                if (out_type) *out_type = (at == AST_TYPE_INT && bt == AST_TYPE_INT) ? AST_TYPE_INT : AST_TYPE_FLOAT;
                return OK;
            }

            ast_type_t at = AST_TYPE_UNKNOWN, bt = AST_TYPE_UNKNOWN;
            const ast_type_t ta = be_infer_expr_type_(be, a);
            const ast_type_t tb = be_infer_expr_type_(be, b);
            const int want_float = (ta == AST_TYPE_FLOAT || tb == AST_TYPE_FLOAT);

            err_t rc = be_emit_expr_(be, a, &at);
            if (rc != OK) return rc;

            if (want_float)
            {
                if (at != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                at = AST_TYPE_FLOAT;
            }
            else
            {
                if (at == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
                at = AST_TYPE_INT;
            }

            rc = be_emit_expr_(be, b, &bt);
            if (rc != OK) return rc;

            if (want_float)
            {
                if (bt != AST_TYPE_FLOAT) be_emitf_(be, "ITOF\n");
                bt = AST_TYPE_FLOAT;
            }
            else
            {
                if (bt == AST_TYPE_FLOAT) be_emitf_(be, "FTOI\n");
                bt = AST_TYPE_INT;
            }

            if (opk == TOK_OP_PLUS)       be_emitf_(be, want_float ? "FADD\n" : "ADD\n");
            else if (opk == TOK_OP_MINUS) be_emitf_(be, want_float ? "FSUB\n" : "SUB\n");
            else if (opk == TOK_OP_MUL)   be_emitf_(be, want_float ? "FMUL\n" : "MUL\n");
            else if (opk == TOK_OP_DIV)   be_emitf_(be, want_float ? "FDIV\n" : "DIV\n");
            else
                BE_FAIL_NODE(be, e, "Unsupported binary operator");

            if (out_type) *out_type = want_float ? AST_TYPE_FLOAT : AST_TYPE_INT;
            return OK;
        }

        default:
            BE_FAIL_NODE(be, e, "Backend: unsupported expr kind %s", ast_kind_to_cstr(e->kind));
    }
}

