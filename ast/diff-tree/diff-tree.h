#ifndef DIFF_TREE_H
#define DIFF_TREE_H

#include "../../libs/logging/logging.h"
#include "../../libs/types.h"
#include "../../libs/io/io.h"
#include "../../libs/hash/hash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#define MAX_RECURSION_LIMIT 4096
#define VARS_MIN_CAP 4

extern const double FLT_ERR;

typedef enum
{
    TYPE_OP  = 0,
    TYPE_VAR = 1,
    TYPE_NUM = 2,
} node_types_e;

#define OPERATION_LIST(X)        \
    X(OP_NOP,  "OP_NOP", 0)      \
    X(OP_ADD,  "+",      1)      \
    X(OP_SUB,  "-",      2)      \
    X(OP_MUL,  "*",      3)      \
    X(OP_DIV,  "/",      4)      \
    X(OP_POW,  "^",      5)      \
    X(OP_SIN,  "sin",    6)      \
    X(OP_COS,  "cos",    7)      \
    X(OP_TAN,  "tan",    8)      \
    X(OP_COT,  "cot",    9)      \
    X(OP_SINH, "sinh",  10)      \
    X(OP_COSH, "cosh",  11)      \
    X(OP_TANH, "tanh",  12)      \
    X(OP_COTH, "coth",  13)      \
    X(OP_LOG,  "log",   14)      \
    X(OP_LN,   "ln",    15)      \
    X(OP_SQRT, "sqrt",  16)      \
    X(OP_ASIN, "asin",  17)      \
    X(OP_ACOS, "acos",  18)      \
    X(OP_ATAN, "atan",  19)      \
    X(OP_ACOT, "acot",  20)

typedef enum
{
#define DEFINE_OP_ENUM(name, str, id) name = id,
    OPERATION_LIST(DEFINE_OP_ENUM)
#undef DEFINE_OP_ENUM
} node_operations_e;

typedef struct
{
    const char*       str;
    size_t            hash;
    node_operations_e op;
} op_t;

typedef struct {
    char*  name;
    size_t hash;
    double value;
} var_t;

typedef union
{
    var_t             var;
    node_operations_e op;
    double            d_value;
    int               i_value;
} node_value_t;

typedef struct node_t
{
    node_types_e node_type;
    node_value_t value;
    
    struct node_t* left;
    struct node_t* right;

    size_t       rank;
} node_t;

typedef struct
{
    size_t  nodes_amount;
    node_t* root;
    var_t*  variables;
    size_t  var_amount;
    size_t  var_capacity;
} tree_t;

typedef struct
{
    char* dump_filename;

    size_t derivative_n;
    size_t taylor_n;
    char   variable;

    double x_from, x_to;
    double y_from, y_to;
    double taylor_y_from, taylor_y_to;
    double step;
    double tangent_x;

    bool plot_original;
    bool plot_tangent_original;
    bool plot_derivative;
    bool plot_tangent_derivative;
    bool plot_taylor;
} derivative_config_t;

#define CREATE_TREE(tree_name) \
    tree_t tree_name;          \
    tree_ctor(&(tree_name))

#define CREATE_NODE(node_name) \
    node_t* node_name = NULL;  \
    node_ctor(&(node_name))

size_t sdbm(const char * str); 

err_t node_ctor(node_t** node);
err_t node_dtor(node_t*  node);

err_t tree_ctor(tree_t * const tree);
err_t tree_dtor(tree_t * const tree);

err_t tree_verify(const tree_t * const tree);

err_t tree_delete_node(node_t * node, size_t iter);
err_t tree_clear      (tree_t * const tree);

err_t tree_insert     (tree_t * const tree, node_t * const node);
err_t tree_read_file  (tree_t *tree, derivative_config_t* config, const char* filename, logging_level level);

err_t tree_parse_expr (tree_t* tree, const char* expr);

var_t* get_or_create_var(tree_t* tree, const char* name);
double evaluate_tree    (tree_t* tree);
void   tree_optimize    (tree_t* tree);

node_t* clone_subtree(const node_t* src,
                      tree_t*       dst,
                      size_t        depth,
                      err_t*        err);

const char* op_to_str(node_operations_e op);

bool   is_same    (double a, double b); 
size_t get_op_rank(node_operations_e op);

#endif

