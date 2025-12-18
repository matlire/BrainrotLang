#ifndef SYNTAX_ANALYZER_H
#define SYNTAX_ANALYZER_H

#include "ast.h"
#include "../lexer/token_list.h"
#include "../lexer/lexer.h"
#include "../libs/types.h"
#include "ast_kinds.h"
#include "../libs/logging/logging.h"
#include "../libs/io/io.h"
#include "diff-tree/diff-tree.h"
#include "diff-tree/differentiation.h"

typedef struct
{
    operational_data_t* op;

    const token_t* tokens;
    size_t         amount;
    size_t         pos;

    ast_tree_t*    ast_tree;

    ast_type_t     cur_func_ret_type;
    int            loop_depth;

    // unresolved function calls => checked after full parse
    struct unresolved_call_s
    {
        size_t      name_id;
        token_pos_t pos;
    } *unresolved;

    size_t unresolved_amount;
    size_t unresolved_cap;
} syntax_analyzer_t;

err_t syntax_analyzer_ctor(syntax_analyzer_t*  sa,
                           operational_data_t* op,
                           const token_t*      tokens,
                           size_t              amount,
                           ast_tree_t*         out_ast);

void  syntax_analyzer_dtor(syntax_analyzer_t* sa);

err_t syntax_analyze(syntax_analyzer_t* sa);

#endif
