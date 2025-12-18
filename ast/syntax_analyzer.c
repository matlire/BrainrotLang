#include "syntax_analyzer.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const token_t* cur_(syntax_analyzer_t* sa)
{
    if (!sa || sa->pos >= sa->amount) return NULL;
    return &sa->tokens[sa->pos];
}

static const token_t* peek_(syntax_analyzer_t* sa, size_t ahead)
{
    if (!sa || sa->pos + ahead >= sa->amount) return NULL;
    return &sa->tokens[sa->pos + ahead];
}

static token_pos_t pos_from_offset_(const operational_data_t* op, size_t offset)
{
    token_pos_t p = { .line = 1, .column = 1, .offset = 0 };
    if (!op || !op->buffer) return p;

    if (offset > op->buffer_size) 
        offset = op->buffer_size;
    p.offset = offset;

    size_t line = 1, col = 1;
    for (size_t i = 0; i < offset; ++i)
    {
        if (op->buffer[i] == '\n') 
            { ++line; col = 1; }
        else 
            ++col;
    }
    p.line = line;
    p.column = col;
    return p;
}

static void set_err_pos_(syntax_analyzer_t* sa, token_pos_t pos, const char* fmt, ...)
{
    if (!sa || !sa->op) return;
    if (sa->op->error_msg[0] != '\0') return;

    sa->op->error_pos = pos.offset;

    char msg[384] = { 0 };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    snprintf(sa->op->error_msg, sizeof(sa->op->error_msg),
             "%s at %zu:%zu (offset: %zu)",
             msg, pos.line, pos.column, pos.offset);
}

static void set_err_(syntax_analyzer_t* sa, const token_t* at, const char* fmt, ...)
{
    if (!sa || !sa->op) return;
    if (sa->op->error_msg[0] != '\0') return;

    token_pos_t pos = at ? at->pos : pos_from_offset_(sa->op, sa->op->buffer_size);
    sa->op->error_pos = pos.offset;

    char msg[384] = { 0 };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    snprintf(sa->op->error_msg, sizeof(sa->op->error_msg),
             "%s at %zu:%zu (offset: %zu)",
             msg, pos.line, pos.column, pos.offset);
}

static int match_(syntax_analyzer_t* sa, token_kind_t kind)
{
    const token_t* tok = cur_(sa);
    if (tok && tok->kind == kind)
    {
        sa->pos++;
        return 1;
    }
    return 0;
}

static int expect_(syntax_analyzer_t* sa, token_kind_t kind, const char* what)
{
    const token_t* tok = cur_(sa);
    if (tok && tok->kind == kind)
    {
        sa->pos++;
        return 1;
    }

    set_err_(sa, tok, "Syntax error: expected %s, got %s",
             what ? what : token_kind_to_cstr(kind),
             tok ? token_kind_to_cstr(tok->kind) : "<eof>");
    return 0;
}

static int is_type_tok_(token_kind_t kind)
{
    return (kind == TOK_KW_NPC) || (kind == TOK_KW_HOMIE) || (kind == TOK_KW_SUS);
}

static ast_type_t type_from_tok_(token_kind_t kind)
{
    switch (kind)
    {
        case TOK_KW_NPC:   return AST_TYPE_INT;
        case TOK_KW_HOMIE: return AST_TYPE_FLOAT;
        case TOK_KW_SUS:   return AST_TYPE_PTR;
        default:           return AST_TYPE_UNKNOWN;
    }
}

static ast_builtin_unary_t builtin_from_tok_(token_kind_t kind)
{
    switch (kind)
    {
        case TOK_KW_STAN:   return AST_BUILTIN_FLOOR;
        case TOK_KW_AURA:   return AST_BUILTIN_CEIL;
        case TOK_KW_DELULU: return AST_BUILTIN_ROUND;
        case TOK_KW_GOOBER: return AST_BUILTIN_ITOF;
        case TOK_KW_BOZO:   return AST_BUILTIN_FTOI;
        default:            return AST_BUILTIN_FLOOR;
    }
}

static size_t require_name_id_(syntax_analyzer_t* sa, const token_t* id_tok, const char* ctx)
{
    if (!id_tok || id_tok->kind != TOK_IDENTIFIER)
        return SIZE_MAX;

    if (id_tok->name_id == SIZE_MAX)
    {
        set_err_(sa, id_tok, "Internal: identifier '%.*s' has no name_id in %s",
                 (int)id_tok->length, id_tok->buffer, ctx ? ctx : "context");
        return SIZE_MAX;
    }
    return id_tok->name_id;
}

static err_t push_unresolved_(syntax_analyzer_t* sa, size_t name_id, token_pos_t pos)
{
    if (sa->unresolved_amount == sa->unresolved_cap)
    {
        size_t new_cap = sa->unresolved_cap ? sa->unresolved_cap * 2 : 8;
        void* p = realloc(sa->unresolved, new_cap * sizeof(*sa->unresolved));
        if (!p) return ERR_ALLOC;
        sa->unresolved = p;
        sa->unresolved_cap = new_cap;
    }
    sa->unresolved[sa->unresolved_amount++] = (struct unresolved_call_s){ name_id, pos };
    return OK;
}

static ast_node_t* parse_program_      (syntax_analyzer_t* sa);

static ast_node_t* parse_function_decl_(syntax_analyzer_t* sa);
static ast_node_t* parse_param_list_   (syntax_analyzer_t* sa);

static ast_node_t* parse_statement_    (syntax_analyzer_t* sa);
static ast_node_t* parse_block_        (syntax_analyzer_t* sa);

static ast_node_t* parse_while_        (syntax_analyzer_t* sa);
static ast_node_t* parse_for_desugared_(syntax_analyzer_t* sa);
static ast_node_t* parse_if_           (syntax_analyzer_t* sa);

static ast_node_t* parse_var_decl_     (syntax_analyzer_t* sa);
static ast_node_t* parse_assignment_   (syntax_analyzer_t* sa);

static ast_node_t* parse_return_       (syntax_analyzer_t* sa);
static ast_node_t* parse_break_        (syntax_analyzer_t* sa);

static ast_node_t* parse_call_stmt_    (syntax_analyzer_t* sa);
static ast_node_t* parse_cout_stmt_    (syntax_analyzer_t* sa, ast_kind_t kind);

static ast_node_t* parse_expr_         (syntax_analyzer_t* sa);
static ast_node_t* parse_or_           (syntax_analyzer_t* sa);
static ast_node_t* parse_and_          (syntax_analyzer_t* sa);
static ast_node_t* parse_eq_           (syntax_analyzer_t* sa);
static ast_node_t* parse_rel_          (syntax_analyzer_t* sa);
static ast_node_t* parse_add_          (syntax_analyzer_t* sa);
static ast_node_t* parse_mul_          (syntax_analyzer_t* sa);
static ast_node_t* parse_pow_          (syntax_analyzer_t* sa);
static ast_node_t* parse_unary_        (syntax_analyzer_t* sa);
static ast_node_t* parse_primary_      (syntax_analyzer_t* sa);

static ast_node_t* parse_call_expr_    (syntax_analyzer_t* sa);
static ast_node_t* parse_arg_list_     (syntax_analyzer_t* sa);

err_t syntax_analyzer_ctor(syntax_analyzer_t*  sa,
                           operational_data_t* op,
                           const token_t*      tokens,
                           size_t              amount,
                           ast_tree_t*         out_ast)
{
    if (!sa || !op || !tokens || !out_ast) return ERR_BAD_ARG;
    memset(sa, 0, sizeof(*sa));
    sa->op       = op;
    sa->tokens   = tokens;
    sa->amount   = amount;
    sa->ast_tree = out_ast;
    return OK;
}

void syntax_analyzer_dtor(syntax_analyzer_t* sa)
{
    if (!sa) return;
    free(sa->unresolved);
    memset(sa, 0, sizeof(*sa));
}

err_t syntax_analyze(syntax_analyzer_t* sa)
{
    if (!sa || !sa->ast_tree || !sa->op) return ERR_BAD_ARG;

    sa->op->error_pos = 0;
    sa->op->error_msg[0] = '\0';

    ast_node_t* prog = parse_program_(sa);
    if (!prog) return ERR_SYNTAX;

    sa->ast_tree->root = prog;

    // resolve forward calls
    for (size_t i = 0; i < sa->unresolved_amount; ++i)
    {
        size_t name_id = sa->unresolved[i].name_id;
        if (symtable_lookup(&sa->ast_tree->symtable, name_id) < 0)
        {
            set_err_pos_(sa, sa->unresolved[i].pos,
                         "Undefined function '%s'",
                         ast_name_cstr(sa->ast_tree, name_id));
            return ERR_SYNTAX;
        }
    }

    return OK;
}

// Parsing helpers
#define SA_CUR()    cur_(sa)
#define SA_PEEK(n)  peek_(sa, (n))
#define SA_POS(tok) ((tok) ? (tok)->pos : (token_pos_t){ 0 })

#define SA_FAIL(tok, ...)                 \
    block_begin                           \
        set_err_(sa, (tok), __VA_ARGS__); \
        return NULL;                      \
    block_end

#define SA_EXPECT(kind, what)             \
    block_begin                           \
        if (!expect_(sa, (kind), (what))) \
            return NULL;                  \
    block_end

#define SA_MATCH(kind) match_(sa, (kind))

#define SA_NEW_NODE(var, kind, tok)                                \
    ast_node_t* var = ast_new(sa->ast_tree, (kind), SA_POS(tok));  \
    block_begin                                                    \
        if (!(var))                                                \
            { set_err_(sa, (tok), "Out of memory"); return NULL; } \
    block_end

#define SA_NEW_AT(var, kind, pos_, tok_for_err)                               \
    ast_node_t* var = ast_new(sa->ast_tree, (kind), (pos_));                  \
    block_begin                                                               \
        if (!(var))                                                           \
            { set_err_(sa, (tok_for_err), "Out of memory"); return NULL; }    \
    block_end

#define SA_PUSH_SCOPE()                                       \
    block_begin                                               \
        if (symtable_push_scope(&sa->ast_tree->symtable) != OK) \
            { SA_FAIL(SA_CUR(), "Out of memory (scope)"); }   \
    block_end

#define SA_POP_SCOPE()                             \
    block_begin                                    \
        symtable_pop_scope(&sa->ast_tree->symtable); \
    block_end

#define SA_DECLARE_OR_FAIL(kind, name_id, type, decl_tok, decl_node)                                 \
    block_begin                                                                                      \
        if (symtable_declare(&sa->ast_tree->symtable, (kind), (name_id), (type), (decl_node)) != OK) \
        { SA_FAIL((decl_tok), "Redeclaration of '%s'", ast_name_cstr(sa->ast_tree, (name_id))); }    \
    block_end

#define SA_MAKE_BIN(var, op_tok, a, b)       \
    SA_NEW_NODE(var, ASTK_BINARY, (op_tok)); \
    (var)->u.binary.op = (op_tok)->kind;     \
    ast_add_child((var), (a));               \
    ast_add_child((var), (b))

#define BINOP_LAYER(FNAME, NEXT, COND)          \
static ast_node_t* FNAME(syntax_analyzer_t* sa) \
{                                               \
    ast_node_t* n = NEXT(sa);                   \
    if (!n) return NULL;                        \
    while (1)                                   \
    {                                           \
        const token_t* op = SA_CUR();           \
        if (!(op && (COND))) break;             \
        sa->pos++;                              \
        ast_node_t* r = NEXT(sa);               \
        if (!r) return NULL;                    \
        SA_MAKE_BIN(bin, op, n, r);             \
        n = bin;                                \
    }                                           \
    return n;                                   \
}

static inline ast_node_t* parse_based_stmt_(syntax_analyzer_t* sa)
{
    return parse_cout_stmt_(sa, ASTK_COUT);
}

static inline ast_node_t* parse_mid_stmt_(syntax_analyzer_t* sa)
{
    return parse_cout_stmt_(sa, ASTK_ICOUT);
}

static inline ast_node_t* parse_peak_stmt_(syntax_analyzer_t* sa)
{
    return parse_cout_stmt_(sa, ASTK_FCOUT);
}

static ast_node_t* last_child_(ast_node_t* parent)
{
    if (!parent) 
        return NULL;

    ast_node_t* c = parent->left;
    if (!c) 
        return NULL;

    while (c->right) 
        c = c->right;
    return c;
}

static int ident_is_(const token_t* tid, const char* s)
{
    const size_t n = strlen(s);
    return tid && tid->buffer && tid->length == n && strncmp(tid->buffer, s, n) == 0;
}

static int is_builtin_call_name_(const token_t* tid)
{
    if (!tid || tid->kind != TOK_IDENTIFIER) return 0;

    if (ident_is_(tid, "in")       || ident_is_(tid, "fin")      || ident_is_(tid, "cin") ||
        ident_is_(tid, "draw")     || ident_is_(tid, "clean_vm") ||
        ident_is_(tid, "out")      || ident_is_(tid, "fout")     || ident_is_(tid, "cout") ||
        ident_is_(tid, "set_pixel"))
        return 1;

    if (ident_is_(tid, "cap")      || ident_is_(tid, "nocap")    || ident_is_(tid, "stinky") ||
        ident_is_(tid, "gyat")     || ident_is_(tid, "skibidi")  ||
        ident_is_(tid, "pookie")   || ident_is_(tid, "rizz")     || ident_is_(tid, "menace"))
        return 1;

    return 0;
}

#define STMT_SEMI_LIST(X)                                        \
    X(TOK_KW_GG,       parse_break_)      /* gg; */              \
    X(TOK_KW_MICDROP,  parse_return_)     /* micdrop [expr]?; */ \
    X(TOK_KW_BRUH,     parse_call_stmt_)  /* bruh f(...); */     \
    X(TOK_KW_BASED,    parse_based_stmt_) /* based(...); */      \
    X(TOK_KW_MID,      parse_mid_stmt_)   /* mid(...); */        \
    X(TOK_KW_PEAK,     parse_peak_stmt_)  /* peak(...); */

// Parsing

// program := function_decl+ EOF
static ast_node_t* parse_program_(syntax_analyzer_t* sa)
{
    const token_t* t0 = SA_CUR();

    SA_NEW_NODE(program, ASTK_PROGRAM, t0);

    int any = 0;

    for (;;)
    {
        const token_t* t = SA_CUR();
        if (!t)
            SA_FAIL(t, "Unexpected end of input");

        if (t->kind == TOK_EOF)
            break;

        ast_node_t* fn = parse_function_decl_(sa);
        if (!fn)
            return NULL;

        ast_add_child(program, fn);
        any = 1;
    }

    if (!any)
        SA_FAIL(SA_CUR(), "Expected at least one function declaration");

    SA_EXPECT(TOK_EOF, "EOF");
    return program;
}

// function_decl := TYPE IDENT "(" param_list ")" block
static ast_node_t* parse_function_decl_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t) return NULL;

    const token_t* tret = SA_CUR();

    ast_type_t ret_type = AST_TYPE_UNKNOWN;

    if (tret && tret->kind == TOK_KW_SIMP)
    {
        ret_type = AST_TYPE_VOID;
        sa->pos++;
    }
    else if (tret && is_type_tok_(tret->kind))
    {
        ret_type = type_from_tok_(tret->kind);
        sa->pos++;
    }
    else
        SA_FAIL(tret, "Expected return type (simp/npc/homie/sus)");

    const token_t* tid = SA_CUR();
    if (!tid || tid->kind != TOK_IDENTIFIER)
        SA_FAIL(tid, "Expected function name identifier");

    size_t fname = require_name_id_(sa, tid, "function name");
    if (fname == SIZE_MAX) return NULL;
    sa->pos++;

    SA_NEW_NODE(fn, ASTK_FUNC, tid);
    fn->u.func.name_id  = fname;
    fn->u.func.ret_type = ret_type;
    fn->type            = ret_type;

    SA_DECLARE_OR_FAIL(SYM_FUNC, fname, ret_type, tid, fn);

    SA_EXPECT(TOK_LPAREN, "(");

    SA_PUSH_SCOPE();

    ast_node_t* plist = parse_param_list_(sa);
    if (!plist) return NULL;

    SA_EXPECT(TOK_RPAREN, ")");

    ast_type_t prev_ret = sa->cur_func_ret_type;
    sa->cur_func_ret_type = ret_type;

    ast_node_t* body = parse_block_(sa);

    sa->cur_func_ret_type = prev_ret;

    if (!body)
        SA_FAIL(SA_CUR(), "Expected function body (yap ... yapity)");

    if (ret_type != AST_TYPE_VOID)
    {
        ast_node_t* last = last_child_(body);
        if (!last || last->kind != ASTK_RETURN)
        {
            token_pos_t p = last ? last->pos : body->pos;

            set_err_pos_(sa, p,
                "Non-void function '%s' must end with 'micdrop <expr>;'.",
                ast_name_cstr(sa->ast_tree, fname));

            return NULL;
        }
    }

    SA_POP_SCOPE();

    ast_add_child(fn, plist);
    ast_add_child(fn, body);
    return fn;
}

// param_list := empty | (type IDENT ("," type IDENT)*)
static ast_node_t* parse_param_list_(syntax_analyzer_t* sa)
{
    const token_t* t0 = SA_CUR();
    SA_NEW_NODE(pl, ASTK_PARAM_LIST, t0);

    const token_t* t = SA_CUR();
    if (!t) return NULL;
    if (t->kind == TOK_RPAREN)
        return pl;

    for (;;)
    {
        const token_t* ttype = SA_CUR();
        if (!ttype || !is_type_tok_(ttype->kind))
            SA_FAIL(ttype, "Expected parameter type (npc/homie/sus)");

        ast_type_t ptype = type_from_tok_(ttype->kind);
        sa->pos++;

        const token_t* tid = SA_CUR();
        if (!tid || tid->kind != TOK_IDENTIFIER)
            SA_FAIL(tid, "Expected parameter name");

        size_t pname = require_name_id_(sa, tid, "param name");
        if (pname == SIZE_MAX) return NULL;

        SA_NEW_NODE(pn, ASTK_PARAM, tid);
        pn->u.param.name_id = pname;
        pn->u.param.type    = ptype;
        pn->type            = ptype;

        SA_DECLARE_OR_FAIL(SYM_PARAM, pname, ptype, tid, pn);

        ast_add_child(pl, pn);
        sa->pos++;

        if (!SA_MATCH(TOK_COMMA))
            break;
    }

    return pl;
}

// block := "yap" statement* "yapity"
static ast_node_t* parse_block_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_YAP)
        return NULL;

    sa->pos++;

    SA_NEW_NODE(block, ASTK_BLOCK, t);

    SA_PUSH_SCOPE();

    for (;;)
    {
        const token_t* c = SA_CUR();
        if (!c)
            SA_FAIL(c, "Unexpected end of input inside block");

        if (c->kind == TOK_KW_YAPITY)
            break;

        ast_node_t* st = parse_statement_(sa);
        if (!st) return NULL;

        ast_add_child(block, st);
    }

    SA_EXPECT(TOK_KW_YAPITY, "yapity");
    SA_POP_SCOPE();

    return block;
}

/*
    STATEMENT ::=
      COMPOUND_STATEMENT
    | WHILE
    | FOR
    | IF
    | VAR_DECL ";"
    | ASSIGNMENT ";"
    | BREAK_STMT ";"
    | RETURN_STMT ";"
    | COUT_STMT ";"
    | ICOUT_STMT ";"
    | FCOUNT_STMT ";"
    | EXPRESSION ";"
    | CALL_STMT ";"
*/
static ast_node_t* parse_statement_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t) return NULL;

    // 1) Structural statements (no semicolon)
    switch (t->kind)
    {
        case TOK_KW_YAP:     return parse_block_(sa);
        case TOK_KW_LOWKEY:  return parse_while_(sa);
        case TOK_KW_HIGHKEY: return parse_for_desugared_(sa);
        case TOK_KW_ALPHA:   return parse_if_(sa);
        default: break;
    }

    // 2) Type-led: variable declaration
    if (is_type_tok_(t->kind))
    {
        ast_node_t* vd = parse_var_decl_(sa);
        if (!vd) return NULL;
        SA_EXPECT(TOK_SEMICOLON, ";");
        return vd;
    }

    // 3) Identifier-led: assignment (lookahead for gaslight)
    if (t->kind == TOK_IDENTIFIER)
    {
        const token_t* t1 = SA_PEEK(1);
        if (t1 && t1->kind == TOK_KW_GASLIGHT)
        {
            ast_node_t* as = parse_assignment_(sa);
            if (!as) return NULL;
            SA_EXPECT(TOK_SEMICOLON, ";");
            return as;
        }
    }

    // 4) Keyword statements that must end with ';'
    switch (t->kind)
    {
#define STMT_CASE(tokkind, fn)             \
        case tokkind: {                    \
            ast_node_t* n = fn(sa);        \
            if (!n) return NULL;           \
            SA_EXPECT(TOK_SEMICOLON, ";"); \
            return n;                      \
        }
        STMT_SEMI_LIST(STMT_CASE)
#undef STMT_CASE
        default: break;
    }

    // 5) Fallback: expression statement
    {
        ast_node_t* e = parse_expr_(sa);
        if (!e) return NULL;

        SA_NEW_AT(st, ASTK_EXPR_STMT, e->pos, SA_CUR());
        ast_add_child(st, e);

        SA_EXPECT(TOK_SEMICOLON, ";");
        return st;
    }
}

// var_decl := type IDENT ["gaslight" expr]
static ast_node_t* parse_var_decl_(syntax_analyzer_t* sa)
{
    const token_t* ttype = SA_CUR();
    if (!ttype || !is_type_tok_(ttype->kind))
        return NULL;

    ast_type_t vtype = type_from_tok_(ttype->kind);
    sa->pos++;

    const token_t* tid = SA_CUR();
    if (!tid || tid->kind != TOK_IDENTIFIER)
        SA_FAIL(tid, "Expected identifier in variable declaration");

    size_t name_id = require_name_id_(sa, tid, "var decl");
    if (name_id == SIZE_MAX) return NULL;

    SA_NEW_NODE(vd, ASTK_VAR_DECL, tid);
    vd->u.vdecl.name_id = name_id;
    vd->u.vdecl.type    = vtype;
    vd->type            = vtype;

    SA_DECLARE_OR_FAIL(SYM_VAR, name_id, vtype, tid, vd);

    sa->pos++;

    if (SA_MATCH(TOK_KW_GASLIGHT))
    {
        ast_node_t* init = parse_expr_(sa);
        if (!init) return NULL;
        ast_add_child(vd, init);
    }

    return vd;
}

// assignment := IDENT "gaslight" expr
static ast_node_t* parse_assignment_(syntax_analyzer_t* sa)
{
    const token_t* tid = SA_CUR();
    if (!tid || tid->kind != TOK_IDENTIFIER)
        return NULL;

    size_t name_id = require_name_id_(sa, tid, "assignment");
    if (name_id == SIZE_MAX) return NULL;

    if (symtable_lookup(&sa->ast_tree->symtable, name_id) < 0)
        SA_FAIL(tid, "Assignment to undeclared identifier '%s'", ast_name_cstr(sa->ast_tree, name_id));

    sa->pos++;

    SA_EXPECT(TOK_KW_GASLIGHT, "gaslight");

    ast_node_t* rhs = parse_expr_(sa);
    if (!rhs) return NULL;

    SA_NEW_NODE(as, ASTK_ASSIGN, tid);
    as->u.assign.name_id = name_id;
    ast_add_child(as, rhs);
    return as;
}

// break := "gg"
static ast_node_t* parse_break_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_GG)
        return NULL;

    if (sa->loop_depth <= 0)
        SA_FAIL(t, "gg (break) outside of loop");

    sa->pos++;

    SA_NEW_NODE(br, ASTK_BREAK, t);
    return br;
}

// return := "micdrop" [expr]?
static ast_node_t* parse_return_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_MICDROP)
        return NULL;

    if (sa->cur_func_ret_type == AST_TYPE_UNKNOWN)
        SA_FAIL(t, "Internal: micdrop used outside of a function");

    sa->pos++;

    SA_NEW_NODE(rn, ASTK_RETURN, t);

    const token_t* c = SA_CUR();
    const int has_expr = (c && c->kind != TOK_SEMICOLON);

    if (sa->cur_func_ret_type == AST_TYPE_VOID)
    {
        if (has_expr)
            SA_FAIL(c, "Void function can't return a value");
        return rn;
    }

    if (!has_expr)
        SA_FAIL(c ? c : t, "Non-void function must return a value");

    ast_node_t* e = parse_expr_(sa);
    if (!e) return NULL;
    ast_add_child(rn, e);

    return rn;
}


// call_stmt := "bruh" IDENT "(" arg_list ")"   (semicolon handled by statement)
static ast_node_t* parse_call_stmt_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_BRUH)
        return NULL;

    sa->pos++;

    const token_t* tid = SA_CUR();
    if (!tid || tid->kind != TOK_IDENTIFIER)
        SA_FAIL(tid, "Expected function name after bruh");

    size_t name_id = require_name_id_(sa, tid, "call stmt");
    if (name_id == SIZE_MAX) return NULL;
    sa->pos++;

    SA_EXPECT(TOK_LPAREN, "(");

    ast_node_t* args = parse_arg_list_(sa);
    if (!args) return NULL;

    SA_EXPECT(TOK_RPAREN, ")");

    SA_NEW_NODE(call, ASTK_CALL, tid);
    call->u.call.name_id = name_id;
    ast_add_child(call, args);

    if (!is_builtin_call_name_(tid) && symtable_lookup(&sa->ast_tree->symtable, name_id) < 0)
    {
        if (push_unresolved_(sa, name_id, tid->pos) != OK)
            SA_FAIL(tid, "Out of memory");
    }

    SA_NEW_NODE(st, ASTK_CALL_STMT, t);
    ast_add_child(st, call);
    return st;
}

// cout_stmt := (based|mid|peak) "(" expr ")"
static ast_node_t* parse_cout_stmt_(syntax_analyzer_t* sa, ast_kind_t kind)
{
    const token_t* t = SA_CUR();
    if (!t) return NULL;

    sa->pos++; /* consume based/mid/peak */

    SA_EXPECT(TOK_LPAREN, "(");

    ast_node_t* e = parse_expr_(sa);
    if (!e) return NULL;

    SA_EXPECT(TOK_RPAREN, ")");

    SA_NEW_NODE(n, kind, t);
    ast_add_child(n, e);
    return n;
}

// while := "lowkey" "(" expr ")" statement
static ast_node_t* parse_while_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_LOWKEY)
        return NULL;

    sa->pos++;

    SA_EXPECT(TOK_LPAREN, "(");

    ast_node_t* cond = parse_expr_(sa);
    if (!cond) return NULL;

    SA_EXPECT(TOK_RPAREN, ")");

    sa->loop_depth++;
    ast_node_t* body = parse_statement_(sa);
    sa->loop_depth--;

    if (!body) return NULL;

    SA_NEW_NODE(w, ASTK_WHILE, t);
    ast_add_child(w, cond);
    ast_add_child(w, body);
    return w;
}

static ast_node_t* make_true_lit_(syntax_analyzer_t* sa, token_pos_t pos)
{
    SA_NEW_AT(n, ASTK_NUM_LIT, pos, SA_CUR());
    n->u.num.lit_type = LIT_INT;
    n->u.num.lit.i64  = 1;
    n->type = AST_TYPE_INT;
    return n;
}

static ast_node_t* wrap_expr_stmt_(syntax_analyzer_t* sa, ast_node_t* e)
{
    if (!e) return NULL;
    SA_NEW_AT(st, ASTK_EXPR_STMT, e->pos, SA_CUR());
    ast_add_child(st, e);
    return st;
}

static ast_node_t* parse_for_desugared_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_HIGHKEY)
        return NULL;

    sa->pos++;

    SA_EXPECT(TOK_LPAREN, "(");

    // init: var_decl | assignment | empty 
    ast_node_t* init = NULL;
    const token_t* c = SA_CUR();
    if (c && c->kind != TOK_SEMICOLON)
    {
        if (is_type_tok_(c->kind))
            init = parse_var_decl_(sa);
        else if (c->kind == TOK_IDENTIFIER && SA_PEEK(1) && SA_PEEK(1)->kind == TOK_KW_GASLIGHT)
            init = parse_assignment_(sa);
        else
            SA_FAIL(c, "Invalid for-init (expected var decl, assignment or empty)");

        if (!init) return NULL;
    }

    SA_EXPECT(TOK_SEMICOLON, ";");

    // cond: expr | empty => true
    ast_node_t* cond = NULL;
    c = SA_CUR();
    if (c && c->kind != TOK_SEMICOLON)
    {
        cond = parse_expr_(sa);
        if (!cond) return NULL;
    }
    else
    {
        token_pos_t p = c ? c->pos : (token_pos_t){0};
        cond = make_true_lit_(sa, p);
        if (!cond) return NULL;
    }

    SA_EXPECT(TOK_SEMICOLON, ";");

    // step: assignment | expr | empty
    ast_node_t* step_stmt = NULL;
    c = SA_CUR();
    if (c && c->kind != TOK_RPAREN)
    {
        ast_node_t* step = NULL;

        if (c->kind == TOK_IDENTIFIER && SA_PEEK(1) && SA_PEEK(1)->kind == TOK_KW_GASLIGHT)
            step = parse_assignment_(sa);
        else
            step = parse_expr_(sa);

        if (!step) return NULL;

        if (step->kind == ASTK_ASSIGN || step->kind == ASTK_VAR_DECL)
            step_stmt = step;
        else
            step_stmt = wrap_expr_stmt_(sa, step);

        if (!step_stmt) return NULL;
    }

    SA_EXPECT(TOK_RPAREN, ")");

    // loop body statement
    sa->loop_depth++;
    ast_node_t* body_stmt = parse_statement_(sa);
    sa->loop_depth--;
    if (!body_stmt) return NULL;

    // Ensure while-body is a block if we need to append step
    ast_node_t* while_body = body_stmt;
    if (step_stmt)
    {
        if (while_body->kind != ASTK_BLOCK)
        {
            SA_NEW_AT(b, ASTK_BLOCK, body_stmt->pos, SA_CUR());
            ast_add_child(b, while_body);
            while_body = b;
        }
        ast_add_child(while_body, step_stmt);
    }

    // while(cond) { ... }
    SA_NEW_NODE(w, ASTK_WHILE, t);
    ast_add_child(w, cond);
    ast_add_child(w, while_body);

    if (!init) return w;

    SA_NEW_NODE(outer, ASTK_BLOCK, t);
    ast_add_child(outer, init);
    ast_add_child(outer, w);
    return outer;
}

static ast_node_t* parse_if_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t || t->kind != TOK_KW_ALPHA)
        return NULL;

    sa->pos++;

    SA_EXPECT(TOK_LPAREN, "(");
    ast_node_t* cond = parse_expr_(sa);
    if (!cond) return NULL;
    SA_EXPECT(TOK_RPAREN, ")");

    ast_node_t* then_st = parse_statement_(sa);
    if (!then_st) return NULL;

    typedef struct { ast_node_t* c; ast_node_t* s; token_pos_t pos; } br_t;
    br_t*   brs   = NULL;
    size_t  brn   = 0;
    size_t  brcap = 0;

    // Collect omega branches
    while (SA_CUR() && SA_CUR()->kind == TOK_KW_OMEGA)
    {
        const token_t* to = SA_CUR();
        sa->pos++;

        SA_EXPECT(TOK_LPAREN, "(");
        ast_node_t* cnd = parse_expr_(sa);
        if (!cnd) { free(brs); return NULL; }
        SA_EXPECT(TOK_RPAREN, ")");

        ast_node_t* st = parse_statement_(sa);
        if (!st) { free(brs); return NULL; }

        if (brn == brcap)
        {
            size_t nc = brcap ? brcap * 2 : 4;
            void* p = realloc(brs, nc * sizeof(*brs));
            if (!p) { free(brs); SA_FAIL(to, "Out of memory"); }
            brs = (br_t*)p;
            brcap = nc;
        }

        brs[brn++] = (br_t){ .c = cnd, .s = st, .pos = to->pos };
    }

    // Optional sigma else
    ast_node_t* tail = NULL;
    if (SA_CUR() && SA_CUR()->kind == TOK_KW_SIGMA)
    {
        const token_t* ts = SA_CUR();
        sa->pos++;

        ast_node_t* else_body = parse_statement_(sa);
        if (!else_body) { free(brs); return NULL; }

        SA_NEW_NODE(els, ASTK_ELSE, ts);
        ast_add_child(els, else_body);
        tail = els;
    }

    for (size_t i = brn; i-- > 0; )
    {
        SA_NEW_AT(br, ASTK_BRANCH, brs[i].pos, SA_CUR());
        ast_add_child(br, brs[i].c);
        ast_add_child(br, brs[i].s);
        if (tail) ast_add_child(br, tail);
        tail = br;
    }
    free(brs);

    SA_NEW_NODE(ifn, ASTK_IF, t);
    ast_add_child(ifn, cond);
    ast_add_child(ifn, then_st);
    if (tail) ast_add_child(ifn, tail);

    return ifn;
}

// expressions: precedence ladder

static ast_node_t* parse_expr_(syntax_analyzer_t* sa) { return parse_or_(sa); }

BINOP_LAYER(parse_or_,  parse_and_,  (op->kind == TOK_OP_OR))
BINOP_LAYER(parse_and_, parse_eq_,   (op->kind == TOK_OP_AND))
BINOP_LAYER(parse_eq_,  parse_rel_,  (op->kind == TOK_OP_EQ || op->kind == TOK_OP_NEQ))
BINOP_LAYER(parse_rel_, parse_add_,  (op->kind == TOK_OP_GT || op->kind == TOK_OP_LT || op->kind == TOK_OP_GTE || op->kind == TOK_OP_LTE))
BINOP_LAYER(parse_add_, parse_mul_,  (op->kind == TOK_OP_PLUS || op->kind == TOK_OP_MINUS))
BINOP_LAYER(parse_mul_, parse_pow_,  (op->kind == TOK_OP_MUL || op->kind == TOK_OP_DIV))
static ast_node_t* parse_pow_(syntax_analyzer_t* sa)
{
    ast_node_t* left = parse_unary_(sa);
    if (!left) return NULL;

    const token_t* op = SA_CUR();
    if (op && op->kind == TOK_OP_POW)
    {
        sa->pos++;
        ast_node_t* right = parse_pow_(sa);
        if (!right) return NULL;
        SA_MAKE_BIN(bin, op, left, right);
        return bin;
    }

    return left;
}

// unary := (! | + | -) unary | primary
static ast_node_t* parse_unary_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t) return NULL;

    if (t->kind == TOK_OP_NOT || t->kind == TOK_OP_MINUS || t->kind == TOK_OP_PLUS)
    {
        sa->pos++;

        ast_node_t* rhs = parse_unary_(sa);
        if (!rhs) return NULL;

        SA_NEW_NODE(u, ASTK_UNARY, t);
        u->u.unary.op = t->kind;
        ast_add_child(u, rhs);
        return u;
    }

    return parse_primary_(sa);
}

/* primary :=
   - "(" expr ")"
   - builtin_unary "(" expr ")"
   - call_expr
   - IDENT
   - NUM
   - STR
*/
static ast_node_t* parse_primary_(syntax_analyzer_t* sa)
{
    const token_t* t = SA_CUR();
    if (!t) return NULL;

    // Parenthesized expression
    if (SA_MATCH(TOK_LPAREN))
    {
        ast_node_t* e = parse_expr_(sa);
        if (!e) return NULL;
        SA_EXPECT(TOK_RPAREN, ")");
        return e;
    }

    // builtin unary: kw "(" expr ")"
    if ((t->kind == TOK_KW_STAN || t->kind == TOK_KW_AURA || t->kind == TOK_KW_DELULU ||
         t->kind == TOK_KW_GOOBER || t->kind == TOK_KW_BOZO) &&
        SA_PEEK(1) && SA_PEEK(1)->kind == TOK_LPAREN)
    {
        token_kind_t bk = t->kind;
        token_pos_t  bp = t->pos;
        sa->pos++;

        SA_EXPECT(TOK_LPAREN, "(");
        ast_node_t* e = parse_expr_(sa);
        if (!e) return NULL;
        SA_EXPECT(TOK_RPAREN, ")");

        SA_NEW_AT(n, ASTK_BUILTIN_UNARY, bp, SA_CUR());
        n->u.builtin_unary.id = builtin_from_tok_(bk);
        ast_add_child(n, e);
        return n;
    }

    // call expr: IDENT "(" ...
    if (t->kind == TOK_IDENTIFIER && SA_PEEK(1) && SA_PEEK(1)->kind == TOK_LPAREN)
        return parse_call_expr_(sa);

    // identifier
    if (t->kind == TOK_IDENTIFIER)
    {
        size_t name_id = require_name_id_(sa, t, "identifier");
        if (name_id == SIZE_MAX) return NULL;

        if (symtable_lookup(&sa->ast_tree->symtable, name_id) < 0)
            SA_FAIL(t, "Use of undeclared identifier '%s'", ast_name_cstr(sa->ast_tree, name_id));

        SA_NEW_NODE(id, ASTK_IDENT, t);
        id->u.ident.name_id = name_id;

        sa->pos++;
        return id;
    }

    // numeric literal
    if (t->kind == TOK_NUMERIC_LITERAL)
    {
        SA_NEW_NODE(n, ASTK_NUM_LIT, t);
        n->u.num.lit_type = t->lit_type;
        n->u.num.lit      = t->lit;
        n->type = (t->lit_type == LIT_FLOAT) ? AST_TYPE_FLOAT : AST_TYPE_INT;

        sa->pos++;
        return n;
    }

    // string literal
    if (t->kind == TOK_STRING_LITERAL)
    {
        SA_NEW_NODE(s, ASTK_STR_LIT, t);
        s->u.str.ptr = t->buffer;
        s->u.str.len = t->length;
        s->type      = AST_TYPE_PTR;

        sa->pos++;
        return s;
    }

    SA_FAIL(t, "Unexpected token in expression: %s", token_kind_to_cstr(t->kind));
}

// call_expr := IDENT "(" arg_list ")"
static ast_node_t* parse_call_expr_(syntax_analyzer_t* sa)
{
    const token_t* tid = SA_CUR();
    if (!tid || tid->kind != TOK_IDENTIFIER)
        return NULL;

    size_t name_id = require_name_id_(sa, tid, "call expr");
    if (name_id == SIZE_MAX) return NULL;

    sa->pos++;

    SA_EXPECT(TOK_LPAREN, "(");

    ast_node_t* args = parse_arg_list_(sa);
    if (!args) return NULL;

    SA_EXPECT(TOK_RPAREN, ")");

    SA_NEW_NODE(call, ASTK_CALL, tid);
    call->u.call.name_id = name_id;
    ast_add_child(call, args);
 
    if (!is_builtin_call_name_(tid) && symtable_lookup(&sa->ast_tree->symtable, name_id) < 0)
    {
        if (push_unresolved_(sa, name_id, tid->pos) != OK)
            SA_FAIL(tid, "Out of memory");
    }

    return call;
}

// arg_list := empty | expr ("," expr)*
static ast_node_t* parse_arg_list_(syntax_analyzer_t* sa)
{
    const token_t* t0 = SA_CUR();
    SA_NEW_NODE(al, ASTK_ARG_LIST, t0);

    const token_t* t = SA_CUR();
    if (t && t->kind == TOK_RPAREN)
        return al;

    ast_node_t* e = parse_expr_(sa);
    if (!e) return NULL;
    ast_add_child(al, e);

    while (SA_MATCH(TOK_COMMA))
    {
        ast_node_t* e2 = parse_expr_(sa);
        if (!e2) return NULL;
        ast_add_child(al, e2);
    }

    return al;
}
