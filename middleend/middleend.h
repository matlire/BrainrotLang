#ifndef MIDDLEEND_H
#define MIDDLEEND_H

#include "../ast/ast.h"

err_t ast_optimize(ast_tree_t* tree, int* out_changed);

#endif
