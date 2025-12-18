#ifndef AST_H
#define AST_H

#include <stddef.h>
#include <stdint.h>

#include "../lexer/token_list.h"
#include "../lexer/lexer.h"
#include "../libs/types.h"
#include "ast_kinds.h"
#include "../libs/logging/logging.h"
#include "../libs/io/io.h"

typedef enum
{
    AST_TYPE_UNKNOWN = 0,
    AST_TYPE_INT,         // npc
    AST_TYPE_FLOAT,       // homie
    AST_TYPE_PTR,         // sus
    AST_TYPE_VOID,
} ast_type_t;

typedef enum
{
#define ASTK_ENUM(sym, str) sym,
    AST_KIND_LIST(ASTK_ENUM)
#undef ASTK_ENUM

    ASTK_COUNT
} ast_kind_t;

typedef enum
{
    AST_BUILTIN_FLOOR = 0, // stan
    AST_BUILTIN_CEIL,      // aura
    AST_BUILTIN_ROUND,     // delulu
    AST_BUILTIN_ITOF,      // goober
    AST_BUILTIN_FTOI,      // bozo
} ast_builtin_unary_t;

typedef struct ast_node_s ast_node_t;

struct ast_node_s
{
    ast_kind_t  kind;
    token_pos_t pos;
    ast_type_t  type;
    
    ast_node_t* parent;
    ast_node_t* left;
    ast_node_t* right;

    union
    {
        struct { size_t name_id; ast_type_t ret_type; } func;
        struct { size_t name_id; ast_type_t type; } param;
        struct { size_t name_id; ast_type_t type; } vdecl;
        struct { size_t name_id; } assign;
        struct { size_t name_id; } ident;
        struct { size_t name_id; } call;

        struct { literal_type_t lit_type; cell64_t lit; } num;
        struct { const char* ptr; size_t len; } str;

        struct { token_kind_t op; } unary;
        struct { token_kind_t op; } binary;

        struct { ast_builtin_unary_t id; } builtin_unary;
        //struct { size_t name_id; int has_value; } deriv;
    } u;
};

typedef enum 
{ 
    SYM_FUNC = 0, 
    SYM_PARAM, 
    SYM_VAR 
} sym_kind_t;

typedef struct
{
    sym_kind_t  kind;
    size_t      name_id;
    ast_type_t  type;
    ast_node_t* decl;
} symbol_t;

typedef struct
{
    size_t first_symbol;
} scope_t;

typedef struct
{
    symbol_t* symbols;
    size_t    amount;
    size_t    cap;

    scope_t* scopes;
    size_t   scopes_amount;
    size_t   scopes_cap;
} symtable_t;

typedef struct
{
    size_t      nodes_amount;
    ast_node_t* root;

    ast_node_t** alloced;
    size_t       alloced_count;
    size_t       alloced_cap;

    nametable_t  nametable;
    symtable_t   symtable;
} ast_tree_t;

err_t ast_tree_ctor(ast_tree_t* ast_tree, nametable_t* nametable);
void  ast_tree_dtor(ast_tree_t* ast_tree);

ast_node_t* ast_new(ast_tree_t* ast_tree, ast_kind_t kind, token_pos_t pos);

void        ast_add_child     (ast_node_t* parent, ast_node_t* child);
ast_node_t* ast_child         (const ast_node_t* node, size_t idx);
size_t      ast_children_count(const ast_node_t* node);

const char* ast_kind_to_cstr(ast_kind_t kind);
const char* ast_type_to_cstr(ast_type_t type);

const char* ast_name_cstr(const ast_tree_t* type, size_t name_id);

void ast_dump_sexpr(FILE* out, const ast_tree_t* ast_tree, const ast_node_t* node);

err_t  symtable_ctor(symtable_t* st);
void   symtable_dtor(symtable_t* st);

err_t  symtable_push_scope(symtable_t* st);
void   symtable_pop_scope (symtable_t* st);

ssize_t symtable_lookup        (const symtable_t* st, size_t name_id);
ssize_t symtable_lookup_current(const symtable_t* st, size_t name_id);
err_t   symtable_declare       (symtable_t* st, sym_kind_t kind, size_t name_id, ast_type_t type, ast_node_t* decl);

err_t ast_read_sexpr_from_op(ast_tree_t* ast_tree, operational_data_t* op);

#endif
