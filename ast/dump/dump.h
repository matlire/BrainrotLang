#ifndef AST_DUMP_H
#define AST_DUMP_H

#include <stdio.h>
#include "ast.h"

void ast_dump_graphviz_html(const ast_tree_t* tree, FILE* out_html);

#endif
