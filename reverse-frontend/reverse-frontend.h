#ifndef REVERSE_FRONTEND_H
#define REVERSE_FRONTEND_H

#include "libs/types.h"
#include "libs/io/io.h"
#include "ast/ast.h"

err_t reverse_frontend_write_rot(operational_data_t* op, const ast_tree_t* ast_tree);

#endif
