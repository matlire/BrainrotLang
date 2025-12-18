#include "ast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

static err_t ensure_cap_(void** buf, size_t* cap, size_t need, size_t elem_size)
{
    if (*cap >= need) return OK;

    size_t new_cap = (*cap ? *cap * 2 : 8);
    if (new_cap < need) new_cap = need;

    void* p = realloc(*buf, new_cap * elem_size);
    if (!p) return ERR_ALLOC;

    *buf = p;
    *cap = new_cap;
    return OK;
}

err_t ast_tree_ctor(ast_tree_t* ast_tree, nametable_t* nametable)
{
    if (!ast_tree) return ERR_BAD_ARG;
    memset(ast_tree, 0, sizeof(*ast_tree));

    if (nametable)
    {
        ast_tree->nametable = *nametable;
        memset(nametable, 0, sizeof(*nametable));
    }
    else
        unused nametable_ctor(&ast_tree->nametable);

    err_t rc = symtable_ctor(&ast_tree->symtable);
    if (rc != OK)
    {
        nametable_dtor(&ast_tree->nametable);
        return rc;
    }

    rc = symtable_push_scope(&ast_tree->symtable);
    if (rc != OK)
    {
        symtable_dtor(&ast_tree->symtable);
        nametable_dtor(&ast_tree->nametable);
        return rc;
    }

    return OK;
}

void ast_tree_dtor(ast_tree_t* ast_tree)
{
    if (!ast_tree) return;

    for (size_t i = 0; i < ast_tree->alloced_count; ++i)
        free(ast_tree->alloced[i]);

    free(ast_tree->alloced);

    symtable_dtor(&ast_tree->symtable);
    nametable_dtor(&ast_tree->nametable);

    memset(ast_tree, 0, sizeof(*ast_tree));
}

ast_node_t* ast_new(ast_tree_t* ast_tree, ast_kind_t kind, token_pos_t pos)
{
    if (!ast_tree) return NULL;

    ast_node_t* node = (ast_node_t*)calloc(1, sizeof(*node));
    if (!node) return NULL;

    node->kind = kind;
    node->pos  = pos;
    node->type = AST_TYPE_UNKNOWN;

    err_t rc = ensure_cap_((void**)&ast_tree->alloced, &ast_tree->alloced_cap, 
                           ast_tree->alloced_count + 1, sizeof(ast_node_t*));
    if (rc != OK)
    {
        free(node);
        return NULL;
    }

    ast_tree->alloced[ast_tree->alloced_count++] = node;
    ast_tree->nodes_amount++;
    return node;
}

void ast_add_child(ast_node_t* parent, ast_node_t* child)
{
    if (!parent || !child) return;

    child->parent = parent;
    child->right  = NULL;

    if (!parent->left)
    {
        parent->left = child;
        return;
    }

    ast_node_t* cur = parent->left;
    while (cur->right) cur = cur->right;
    cur->right = child;
}

ast_node_t* ast_child(const ast_node_t* node, size_t idx)
{
    if (!node) return NULL;
    ast_node_t* c = node->left;
    while (c && idx--) c = c->right;
    return c;
}

size_t ast_children_count(const ast_node_t* node)
{
    if (!node) return 0;
    size_t cnt = 0;
    for (ast_node_t* c = node->left; c; c = c->right) cnt++;
    return cnt;
}

const char* ast_name_cstr(const ast_tree_t* ast_tree, size_t name_id)
{
    if (!ast_tree) return NULL;
    if (name_id == SIZE_MAX) return NULL;
    if (name_id >= ast_tree->nametable.amount) return NULL;
    return ast_tree->nametable.data[name_id].name;
}

const char* ast_kind_to_cstr(ast_kind_t kind)
{
    switch (kind)
    {
#define ASTK_CASE(sym, str) case sym: return str;
        AST_KIND_LIST(ASTK_CASE)
#undef ASTK_CASE
        default: return "<ast-kind?>";
    }
}

const char* ast_type_to_cstr(ast_type_t type)
{
    switch (type)
    {
        case AST_TYPE_UNKNOWN: return "unknown";
        case AST_TYPE_INT:     return "int";
        case AST_TYPE_FLOAT:   return "float";
        case AST_TYPE_PTR:     return "ptr";
        case AST_TYPE_VOID:    return "void";
        default:               return "<?>";
    }
}

err_t symtable_ctor(symtable_t* st)
{
    if (!st) return ERR_BAD_ARG;
    memset(st, 0, sizeof(*st));
    return OK;
}

void symtable_dtor(symtable_t* st)
{
    if (!st) return;
    free(st->symbols);
    free(st->scopes);
    memset(st, 0, sizeof(*st));
}

err_t symtable_push_scope(symtable_t* st)
{
    if (!st) return ERR_BAD_ARG;

    err_t rc = ensure_cap_((void**)&st->scopes, &st->scopes_cap, st->scopes_amount + 1, sizeof(scope_t));
    if (rc != OK)
        return rc;

    scope_t* s = &st->scopes[st->scopes_amount++];
    s->first_symbol = st->amount;
    return OK;
}

void symtable_pop_scope(symtable_t* st)
{
    if (!st || st->scopes_amount == 0) return;

    size_t first = st->scopes[st->scopes_amount - 1].first_symbol;
    st->amount = first;
    st->scopes_amount--;
}

static ssize_t lookup_range_(const symtable_t* st, size_t name_id, size_t from, size_t to_excl)
{
    for (size_t i = to_excl; i-- > from; )
        if (st->symbols[i].name_id == name_id)
            return (ssize_t)i;
    return -1;
}

ssize_t symtable_lookup_current(const symtable_t* st, size_t name_id)
{
    if (!st || st->scopes_amount == 0) return -1;

    size_t from = st->scopes[st->scopes_amount - 1].first_symbol;
    return lookup_range_(st, name_id, from, st->amount);
}

ssize_t symtable_lookup(const symtable_t* st, size_t name_id)
{
    if (!st || st->scopes_amount == 0) return -1;

    size_t to = st->amount;
    for (size_t s = st->scopes_amount; s-- > 0; )
    {
        size_t from = st->scopes[s].first_symbol;
        ssize_t idx = lookup_range_(st, name_id, from, to);
        if (idx >= 0) return idx;
        to = from;
    }
    return -1;
}

err_t symtable_declare(symtable_t* st, sym_kind_t kind, size_t name_id, ast_type_t type, ast_node_t* decl)
{
    if (!st) return ERR_BAD_ARG;

    // no redeclare in same scope
    if (symtable_lookup_current(st, name_id) >= 0)
        return ERR_SYNTAX;

    err_t rc = ensure_cap_((void**)&st->symbols, &st->cap, st->amount + 1, sizeof(symbol_t));
    if (rc != OK)
        return rc;

    symbol_t* sym = &st->symbols[st->amount++];
    sym->kind    = kind;
    sym->name_id = name_id;
    sym->type    = type;
    sym->decl    = decl;
    return OK;
}

#define AST_DUMP_PAYLOAD_LIST(X)                  \
    X(ASTK_FUNC, {                                \
        fprintf(out, " name=%s ret=%s",           \
            ast_name_cstr(t, n->u.func.name_id),  \
            ast_type_to_cstr(n->u.func.ret_type));\
    })                                            \
    X(ASTK_PARAM, {                               \
        fprintf(out, " name=%s type=%s",          \
            ast_name_cstr(t, n->u.param.name_id), \
            ast_type_to_cstr(n->u.param.type));   \
    })                                            \
    X(ASTK_VAR_DECL, {                            \
        fprintf(out, " name=%s type=%s",          \
            ast_name_cstr(t, n->u.vdecl.name_id), \
            ast_type_to_cstr(n->u.vdecl.type));   \
    })                                            \
    X(ASTK_ASSIGN, {                                                     \
        fprintf(out, " name=%s", ast_name_cstr(t, n->u.assign.name_id)); \
    })                                                                   \
    X(ASTK_IDENT, {                                                      \
        fprintf(out, " name=%s", ast_name_cstr(t, n->u.ident.name_id));  \
    })                                                                   \
    X(ASTK_CALL, {                                                       \
        fprintf(out, " name=%s", ast_name_cstr(t, n->u.call.name_id));   \
    })                                                                   \
    X(ASTK_NUM_LIT, {                                                    \
        if (n->u.num.lit_type == LIT_INT)                                \
            fprintf(out, " int=%lld", (long long)n->u.num.lit.i64);      \
        else if (n->u.num.lit_type == LIT_FLOAT)                         \
            fprintf(out, " float=%g", n->u.num.lit.f64);                 \
    })                                                                   \
    X(ASTK_STR_LIT, {                                                    \
        fprintf(out, " str_len=%zu", n->u.str.len);                      \
    })                                                                   \
    X(ASTK_UNARY, {                                                      \
        fprintf(out, " op=%s", token_kind_to_cstr(n->u.unary.op));       \
    })                                                                   \
    X(ASTK_BINARY, {                                                     \
        fprintf(out, " op=%s", token_kind_to_cstr(n->u.binary.op));      \
    })                                                                   \
    X(ASTK_BUILTIN_UNARY, {                                              \
        fprintf(out, " builtin=%d", (int)n->u.builtin_unary.id);         \
    })

static void dump_payload_(FILE* out, const ast_tree_t* t, const ast_node_t* n)
{
    switch (n->kind)
    {
#define AST_PAYLOAD_CASE(kind, code) case kind: code break;
        AST_DUMP_PAYLOAD_LIST(AST_PAYLOAD_CASE)
#undef AST_PAYLOAD_CASE
        default: break;
    }
}

void ast_dump_sexpr(FILE* out, const ast_tree_t* ast_tree, const ast_node_t* node)
{
    if (!out) return;
    if (!node) { fprintf(out, "nil"); return; }

    fprintf(out, "( %s", ast_kind_to_cstr(node->kind));
    dump_payload_(out, ast_tree, node);
    fprintf(out, " ");

    // left = first child
    if (node->left) ast_dump_sexpr(out, ast_tree, node->left);
    else            fprintf(out, "nil");

    fprintf(out, " ");

    // right = next sibling
    if (node->right) ast_dump_sexpr(out, ast_tree, node->right);
    else             fprintf(out, "nil");

    fprintf(out, " )");
}

#undef AST_DUMP_PAYLOAD_LIST

typedef struct
{
    operational_data_t* op;
    const char*         buffer;
    size_t              len;
    size_t              offset;
} sxr_t;

static void sxr_linecol_(const char* buffer, size_t n, size_t offset, size_t* line, size_t* col)
{
    size_t L = 1, C = 1;
    if (offset > n) offset = n;

    for (size_t k = 0; k < offset; ++k)
    {
        if (buffer[k] == '\n') { ++L; C = 1; }
        else { ++C; }
    }

    *line = L;
    *col  = C;
}

static err_t sxr_fail_(sxr_t* r, const char* msg)
{
    if (!r || !r->op) return ERR_SYNTAX;

    if (r->op->error_msg[0] != '\0')
        return ERR_SYNTAX;

    size_t line = 1, col = 1;
    sxr_linecol_(r->buffer, r->len, r->offset, &line, &col);

    r->op->error_pos = r->offset;
    snprintf(r->op->error_msg, sizeof(r->op->error_msg),
             "%s at %zu:%zu (offset: %zu)", msg, line, col, r->offset);
    return ERR_SYNTAX;
}

static void sxr_skip_ws_(sxr_t* r)
{
    while (r->offset < r->len && isspace((unsigned char)r->buffer[r->offset]))
        r->offset++;
}

static int sxr_consume_(sxr_t* r, char ch)
{
    sxr_skip_ws_(r);
    if (r->offset < r->len && r->buffer[r->offset] == ch)
    {
        r->offset++;
        return 1;
    }
    return 0;
}

static int sxr_is_delim_(char c)
{
    return isspace((unsigned char)c) || c == '(' || c == ')' || c == '\0';
}

static int sxr_atom_(sxr_t* r, char* dst, size_t cap)
{
    sxr_skip_ws_(r);

    if (r->offset >= r->len) return 0;
    char c = r->buffer[r->offset];
    if (c == '(' || c == ')') return 0;

    size_t start = r->offset;
    while (r->offset < r->len && !sxr_is_delim_(r->buffer[r->offset]))
        r->offset++;

    size_t n = r->offset - start;
    if (n == 0) return 0;

    if (n >= cap) n = cap - 1;
    memcpy(dst, r->buffer + start, n);
    dst[n] = '\0';
    return 1;
}

static int sxr_next_is_payload_(sxr_t* r)
{
    sxr_skip_ws_(r);
    if (r->offset >= r->len) return 0;

    char c = r->buffer[r->offset];
    if (c == '(' || c == ')') return 0;

    size_t j = r->offset;
    while (j < r->len && !sxr_is_delim_(r->buffer[j]))
    {
        if (r->buffer[j] == '=') return 1;
        j++;
    }
    return 0;
}

static int ast_kind_from_text_(const char* s, ast_kind_t* out)
{
    if (!s || !out) return 0;

    for (int k = 0; k < (int)ASTK_COUNT; ++k)
    {
        const char* ks = ast_kind_to_cstr((ast_kind_t)k);
        if (ks && strcmp(ks, s) == 0)
        {
            *out = (ast_kind_t)k;
            return 1;
        }
    }
    return 0;
}

static ast_type_t ast_type_from_text_(const char* s)
{
    if (!s) return AST_TYPE_UNKNOWN;
    if (strcmp(s, "int") == 0)   return AST_TYPE_INT;
    if (strcmp(s, "float") == 0) return AST_TYPE_FLOAT;
    if (strcmp(s, "ptr") == 0)   return AST_TYPE_PTR;
    if (strcmp(s, "void") == 0)  return AST_TYPE_VOID;
    return AST_TYPE_UNKNOWN;
}

static token_kind_t token_kind_from_text_(const char* s)
{
    if (!s) return TOK_ERROR;

    for (int k = 0; k < (int)TOK_COUNT; ++k)
    {
        const char* ks = token_kind_to_cstr((token_kind_t)k);
        if (ks && strcmp(ks, s) == 0)
            return (token_kind_t)k;
    }
    return TOK_ERROR;
}

static err_t sxr_apply_payload_(ast_tree_t* t, sxr_t* r, ast_node_t* n, const char* atom)
{
    (void)r;

    const char* eq = strchr(atom, '=');
    if (!eq) return OK;

    size_t klen = (size_t)(eq - atom);
    const char* val = eq + 1;

#define KEY(lit) (klen == sizeof(lit)-1 && strncmp(atom, (lit), sizeof(lit)-1) == 0)

    if (KEY("name"))
    {
        size_t id = nametable_insert(&t->nametable, val, strlen(val));
        if (id == SIZE_MAX) return ERR_ALLOC;

        switch (n->kind)
        {
            case ASTK_FUNC:     n->u.func.name_id   = id; break;
            case ASTK_PARAM:    n->u.param.name_id  = id; break;
            case ASTK_VAR_DECL: n->u.vdecl.name_id  = id; break;
            case ASTK_ASSIGN:   n->u.assign.name_id = id; break;
            case ASTK_IDENT:    n->u.ident.name_id  = id; break;
            case ASTK_CALL:     n->u.call.name_id   = id; break;
            default: break;
        }
        return OK;
    }

    if (KEY("ret") && n->kind == ASTK_FUNC)
    {
        n->u.func.ret_type = ast_type_from_text_(val);
        return OK;
    }

    if (KEY("type"))
    {
        ast_type_t ty = ast_type_from_text_(val);

        if (n->kind == ASTK_PARAM)    n->u.param.type = ty;
        if (n->kind == ASTK_VAR_DECL) n->u.vdecl.type = ty;
        return OK;
    }

    if (KEY("int") && n->kind == ASTK_NUM_LIT)
    {
        errno = 0;
        long long v = strtoll(val, NULL, 10);
        if (errno != 0) return ERR_SYNTAX;

        n->u.num.lit_type = LIT_INT;
        n->u.num.lit.i64  = (int64_t)v;
        n->type = AST_TYPE_INT;
        return OK;
    }

    if (KEY("float") && n->kind == ASTK_NUM_LIT)
    {
        errno = 0;
        double v = strtod(val, NULL);
        if (errno != 0) return ERR_SYNTAX;

        n->u.num.lit_type = LIT_FLOAT;
        n->u.num.lit.f64  = v;
        n->type = AST_TYPE_FLOAT;
        return OK;
    }

    if (KEY("op"))
    {
        token_kind_t opk = token_kind_from_text_(val);
        if (opk == TOK_ERROR) return ERR_SYNTAX;

        if (n->kind == ASTK_UNARY)  n->u.unary.op  = opk;
        if (n->kind == ASTK_BINARY) n->u.binary.op = opk;
        return OK;
    }

    if (KEY("builtin") && n->kind == ASTK_BUILTIN_UNARY)
    {
        n->u.builtin_unary.id = (ast_builtin_unary_t)strtol(val, NULL, 10);
        return OK;
    }

    if (KEY("str_len") && n->kind == ASTK_STR_LIT)
    {
        n->u.str.len = (size_t)strtoull(val, NULL, 10);
        n->u.str.ptr = NULL;
        return OK;
    }

#undef KEY
    return OK;
}

static ast_node_t* sxr_parse_node_(ast_tree_t* t, sxr_t* r, ast_node_t* parent);

static ast_node_t* sxr_parse_node_or_nil_(ast_tree_t* t, sxr_t* r, ast_node_t* parent)
{
    sxr_skip_ws_(r);

    size_t save = r->offset;
    char a[32] = { 0 };
    if (sxr_atom_(r, a, sizeof(a)))
    {
        if (strcmp(a, "nil") == 0)
            return NULL;

        r->offset = save;
    }

    return sxr_parse_node_(t, r, parent);
}

static ast_node_t* sxr_parse_node_(ast_tree_t* t, sxr_t* r, ast_node_t* parent)
{
    if (!sxr_consume_(r, '('))
    {
        sxr_fail_(r, "Expected '('");
        return NULL;
    }

    char kind_txt[64] = { 0 };
    if (!sxr_atom_(r, kind_txt, sizeof(kind_txt)))
    {
        sxr_fail_(r, "Expected AST kind");
        return NULL;
    }

    ast_kind_t kind = ASTK_EMPTY;
    if (!ast_kind_from_text_(kind_txt, &kind))
    {
        sxr_fail_(r, "Unknown AST kind");
        return NULL;
    }

    size_t line = 1, col = 1;
    sxr_linecol_(r->buffer, r->len, r->offset, &line, &col);
    token_pos_t pos = { .line = line, .column = col, .offset = r->offset };

    ast_node_t* n = ast_new(t, kind, pos);
    if (!n)
    {
        sxr_fail_(r, "Out of memory creating AST node");
        return NULL;
    }
    n->parent = parent;

    while (sxr_next_is_payload_(r))
    {
        char kv[256] = {0};
        if (!sxr_atom_(r, kv, sizeof(kv)))
            break;

        err_t rc = sxr_apply_payload_(t, r, n, kv);
        if (rc != OK)
        {
            sxr_fail_(r, "Bad payload atom");
            return NULL;
        }
    }

    n->left = sxr_parse_node_or_nil_(t, r, n);
    if (r->op->error_msg[0]) return NULL;

    n->right = sxr_parse_node_or_nil_(t, r, parent);
    if (r->op->error_msg[0]) return NULL;

    if (!sxr_consume_(r, ')'))
    {
        sxr_fail_(r, "Expected ')'");
        return NULL;
    }

    return n;
}

static err_t sxr_read_all_into_op_buffer_(operational_data_t* op)
{
    if (!op || !op->in_file) return ERR_BAD_ARG;

    free(op->buffer);
    op->buffer = NULL;
    op->buffer_size = 0;

    size_t cap = 4096;
    char* buf = (char*)malloc(cap + 1);
    if (!buf) return ERR_ALLOC;

    size_t n = 0;
    for (;;)
    {
        if (n == cap)
        {
            size_t new_cap = cap * 2;
            char* nb = (char*)realloc(buf, new_cap + 1);
            if (!nb)
            {
                free(buf);
                return ERR_ALLOC;
            }
            buf = nb;
            cap = new_cap;
        }

        size_t want = cap - n;
        size_t got = fread(buf + n, 1, want, op->in_file);
        n += got;

        if (got < want)
        {
            if (feof(op->in_file)) break;
            if (ferror(op->in_file))
            {
                free(buf);
                return ERR_CORRUPT;
            }
        }
    }

    buf[n]          = '\0';
    op->buffer      = buf;
    op->buffer_size = n;
    return OK;
}

err_t ast_read_sexpr_from_op(ast_tree_t* ast_tree, operational_data_t* op)
{
    if (!ast_tree || !op || !op->in_file) return ERR_BAD_ARG;

    op->error_pos = 0;
    op->error_msg[0] = '\0';

    err_t rc = sxr_read_all_into_op_buffer_(op);
    if (rc != OK)
    {
        snprintf(op->error_msg, sizeof(op->error_msg),
                 "Failed to read AST input stream (err=%d)", rc);
        return rc;
    }

    sxr_t r = { .op = op, .buffer = op->buffer, .len = op->buffer_size, .offset = 0 };

    ast_node_t* root = sxr_parse_node_or_nil_(ast_tree, &r, NULL);
    if (!root)
        return (op->error_msg[0] ? ERR_SYNTAX : sxr_fail_(&r, "Failed to parse AST root"));

    sxr_skip_ws_(&r);
    if (r.offset < r.len)
        return sxr_fail_(&r, "Trailing garbage after AST");

    ast_tree->root = root;
    return OK;
}

