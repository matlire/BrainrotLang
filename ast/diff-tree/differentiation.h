#ifndef DIFFERENTIATION_H
#define DIFFERENTIATION_H

#include "diff-tree.h"
#include "../../libs/io/io.h"

err_t tree_derivative(tree_t* in_tree,
                      tree_t* out_tree,
                      derivative_config_t* config);

err_t tree_derivative_n(tree_t*     in_tree,
                        tree_t*     out_tree,
                        const char* var_name,
                        size_t      derivative_n);

#endif
