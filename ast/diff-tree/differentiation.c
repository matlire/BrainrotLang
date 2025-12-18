#include "differentiation.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static node_t* alloc_node(tree_t* out_tree, err_t* err)
{
    if (!out_tree || !err || *err != OK)
        return NULL;

    node_t* n = NULL;
    *err = node_ctor(&n);
    if (*err != OK || !n)
    {
        *err = ERR_ALLOC;
        return NULL;
    }

    n->left  = NULL;
    n->right = NULL;
    out_tree->nodes_amount++;
    return n;
}

static node_t* new_num(tree_t* out_tree, double v, err_t* err)
{
    node_t* n = alloc_node(out_tree, err);
    if (!n) return NULL;

    n->node_type     = TYPE_NUM;
    n->value.d_value = v;
    n->rank          = 100;
    return n;
}

static node_t* new_op(tree_t* out_tree,
                      node_operations_e op,
                      node_t*           left,
                      node_t*           right,
                      err_t*            err)
{
    node_t* n = alloc_node(out_tree, err);
    if (!n) return NULL;

    n->node_type = TYPE_OP;
    n->value.op  = op;
    n->left      = left;
    n->right     = right;
    n->rank      = get_op_rank(op);
    return n;
}

static int subtree_holds_var(const node_t* node, size_t var_hash)
{
    if (!node)
        return 0;

    if (node->node_type == TYPE_VAR &&
        node->value.var.hash == var_hash)
        return 1;

    return subtree_holds_var(node->left,  var_hash) ||
           subtree_holds_var(node->right, var_hash);
}

static node_t* diff_node(const node_t* node,
                         tree_t*       out_tree,
                         size_t        var_hash,
                         err_t*        err);

#define cL clone_subtree(node->left,  out_tree, 0, err)
#define cR clone_subtree(node->right, out_tree, 0, err)
#define dL diff_node(node->left,  out_tree, var_hash, err)
#define dR diff_node(node->right, out_tree, var_hash, err)

#define CONST_(num_)      new_num(out_tree, (num_), err)

#define ADD_(L_, R_)      new_op(out_tree, OP_ADD,  (L_), (R_), err)
#define SUB_(L_, R_)      new_op(out_tree, OP_SUB,  (L_), (R_), err)
#define MUL_(L_, R_)      new_op(out_tree, OP_MUL,  (L_), (R_), err)
#define DIV_(L_, R_)      new_op(out_tree, OP_DIV,  (L_), (R_), err)
#define POW_(L_, R_)      new_op(out_tree, OP_POW,  (L_), (R_), err)

#define SIN_(A_)          new_op(out_tree, OP_SIN,  (A_), NULL, err)
#define COS_(A_)          new_op(out_tree, OP_COS,  (A_), NULL, err)
#define TAN_(A_)          new_op(out_tree, OP_TAN,  (A_), NULL, err)
#define COT_(A_)          new_op(out_tree, OP_COT,  (A_), NULL, err)

#define SINH_(A_)         new_op(out_tree, OP_SINH, (A_), NULL, err)
#define COSH_(A_)         new_op(out_tree, OP_COSH, (A_), NULL, err)
#define TANH_(A_)         new_op(out_tree, OP_TANH, (A_), NULL, err)
#define COTH_(A_)         new_op(out_tree, OP_COTH, (A_), NULL, err)

#define LN_(A_)           new_op(out_tree, OP_LN,   (A_), NULL, err)
#define SQRT_(A_)         new_op(out_tree, OP_SQRT, (A_), NULL, err)

static node_t* diff_op(const node_t* node,
                       tree_t*       out_tree,
                       size_t        var_hash,
                       err_t*        err)
{
    switch (node->value.op)
    {
        case OP_ADD:  return ADD_(dL, dR);
        case OP_SUB:  return SUB_(dL, dR);
        case OP_MUL:  return ADD_(MUL_(dL, cR), MUL_(cL, dR));
        case OP_DIV:  return DIV_(SUB_(MUL_(dL, cR), MUL_(cL, dR)),
                                  POW_(cR, CONST_(2.0)));
// Вынести
        case OP_POW:
        {
            int left_dep  = subtree_holds_var(node->left,  var_hash);
            int right_dep = subtree_holds_var(node->right, var_hash);

            if (!left_dep && !right_dep)
                return CONST_(0.0);
            else if (left_dep && !right_dep)
                return MUL_(MUL_(cR,
                                 POW_(cL, SUB_(cR, CONST_(1.0)))),
                            dL);
            else if (!left_dep && right_dep)
                return MUL_(MUL_(POW_(cL, cR),
                                 LN_(cL)),
                            dR);
            else
                return MUL_(POW_(cL, cR),
                            ADD_(MUL_(dR, LN_(cL)),
                                 MUL_(cR, DIV_(dL, cL))));
        }

        case OP_LN:   return DIV_(dL, cL);

        case OP_LOG:
            return DIV_(SUB_(MUL_(DIV_(dR, cR), LN_(cL)),
                             MUL_(DIV_(dL, cL), LN_(cR))),
                        POW_(LN_(cL), CONST_(2.0)));

        case OP_SQRT: return DIV_(dL, MUL_(CONST_(2.0), SQRT_(cL)));

        case OP_SIN:  return MUL_(COS_(cL), dL);
        case OP_COS:  return MUL_(CONST_(-1.0), MUL_(SIN_(cL), dL));
        case OP_TAN:  return DIV_(dL, POW_(COS_(cL), CONST_(2.0)));
        case OP_COT:  return MUL_(CONST_(-1.0),
                                  DIV_(dL, POW_(SIN_(cL), CONST_(2.0))));

        case OP_SINH: return MUL_(COSH_(cL), dL);
        case OP_COSH: return MUL_(SINH_(cL), dL);
        case OP_TANH: return DIV_(dL, POW_(COSH_(cL), CONST_(2.0)));
        case OP_COTH: return MUL_(CONST_(-1.0),
                                  DIV_(dL, POW_(SINH_(cL), CONST_(2.0))));

        case OP_ASIN: return DIV_(dL,
                                  SQRT_(SUB_(CONST_(1.0),
                                             POW_(cL, CONST_(2.0)))));
        case OP_ACOS: return MUL_(CONST_(-1.0),
                                  DIV_(dL,
                                       SQRT_(SUB_(CONST_(1.0),
                                                  POW_(cL, CONST_(2.0))))));
        case OP_ATAN: return DIV_(dL,
                                  ADD_(CONST_(1.0),
                                       POW_(cL, CONST_(2.0))));
        case OP_ACOT: return MUL_(CONST_(-1.0),
                                  DIV_(dL,
                                       ADD_(CONST_(1.0),
                                            POW_(cL, CONST_(2.0)))));

        case OP_NOP:
        default:
            return node->left ? dL : CONST_(0.0);
    }
}

static node_t* diff_node(const node_t* node,
                         tree_t*       out_tree,
                         size_t        var_hash,
                         err_t*        err)
{
    if (!node || *err != OK)
        return NULL;

    switch (node->node_type)
    {
        case TYPE_NUM:
            return CONST_(0.0);

        case TYPE_VAR:
            if (node->value.var.hash == var_hash)
                return CONST_(1.0);
            else
                return CONST_(0.0);

        case TYPE_OP:
            return diff_op(node, out_tree, var_hash, err);

        default:
            *err = ERR_CORRUPT;
            return NULL;
    }
}

static size_t count_nodes_rec(const node_t* n)
{
    if (!n) return 0;
    return 1 + count_nodes_rec(n->left) + count_nodes_rec(n->right);
}

static err_t tree_clone_into(const tree_t* src, tree_t* dst)
{
    if (!src || !dst)
        return ERR_BAD_ARG;

    err_t err = OK;

    tree_clear(dst);

    if (!src->root)
    {
        dst->root         = NULL;
        dst->nodes_amount = 0;
        return OK;
    }

    dst->root = clone_subtree(src->root, dst, 0, &err);
    if (err != OK || !dst->root)
    {
        tree_clear(dst);
        return err ? err : ERR_CORRUPT;
    }

    dst->nodes_amount = count_nodes_rec(dst->root);
    return OK;
}

static err_t tree_derivative_once(const tree_t* in_tree,
                                  tree_t*       out_tree,
                                  size_t        var_hash)
{
    if (!in_tree || !out_tree)
        return ERR_BAD_ARG;

    err_t err = OK;
    out_tree->root         = NULL;
    out_tree->nodes_amount = 0;

    if (!in_tree->root)
        return OK;

    node_t* new_root = diff_node(in_tree->root, out_tree, var_hash, &err);
    if (err != OK || !new_root)
    {
        tree_clear(out_tree);
        return err ? err : ERR_CORRUPT;
    }

    out_tree->root         = new_root;
    out_tree->nodes_amount = count_nodes_rec(new_root);

    return OK;
}

static err_t tree_derivative_plain(const tree_t* in_tree,
                                   tree_t*       out_tree,
                                   size_t        var_hash,
                                   size_t        n)
{
    if (!in_tree || !out_tree)
        return ERR_BAD_ARG;

    if (n == 0)
        return tree_clone_into(in_tree, out_tree);

    tree_t current;
    err_t err = tree_ctor(&current);
    if (err != OK)
        return err;

    err = tree_clone_into(in_tree, &current);
    if (err != OK)
    {
        tree_clear(&current);
        return err;
    }

    for (size_t i = 0; i < n; ++i)
    {
        tree_clear(out_tree);

        err = tree_derivative_once(&current, out_tree, var_hash);
        
        if (err != OK)
        {
            tree_clear(&current);
            tree_clear(out_tree);
            return err;
        }
        tree_optimize(out_tree);

        if (i + 1 < n)
        {
            err = tree_clone_into(out_tree, &current);
            if (err != OK)
            {
                tree_clear(&current);
                tree_clear(out_tree);
                return err;
            }
        }
    }

    tree_clear(&current);
    return OK;
}
/*
static void dump_initial_function_and_plots(tree_t*              in_tree,
                                            derivative_config_t* config)
{
    if (!in_tree || !config)
        return;

    double tangent_x = config->tangent_x;

    tree_dump_latex(in_tree, config, "Исходная функция:");

    if (config->plot_original)
        tree_dump_plot(in_tree, config, "График:");

    if (config->plot_tangent_original)
    {
        char plt_comment[256] = {0};
        snprintf(plt_comment, sizeof(plt_comment),
                 "График касательной к исходной функции в точке %lg:",
                 tangent_x);
        tree_dump_plot_tangent(in_tree, config, plt_comment);
    }

    if (config->plot_taylor)
    {
        char plt_comment[256] = {0};
        snprintf(plt_comment, sizeof(plt_comment),
                 "Общий график:");
        tree_dump_full_plot(in_tree, config, plt_comment);
    }

    tree_dump_heading(config, "Решение");
}

static err_t tree_derivative_zero_with_dump(tree_t*              in_tree,
                                            tree_t*              out_tree,
                                            derivative_config_t* config)
{
    if (!in_tree || !out_tree || !config)
        return ERR_BAD_ARG;

    double tangent_x = config->tangent_x;

    err_t err = tree_clone_into(in_tree, out_tree);
    if (err != OK)
    {
        tree_clear(out_tree);
        return err;
    }

    tree_dump_random_meme(config);

    tree_dump_latex(out_tree, config,
                    "Нулевая производная по x совпадает с функцией:");

    if (config->plot_derivative)
    {
        tree_dump_plot(out_tree, config, "График функции:");
    }

    if (config->plot_tangent_derivative)
    {
        char plt_comment[256] = {0};
        snprintf(plt_comment, sizeof(plt_comment),
                 "График касательной к функции в точке %lg:",
                 tangent_x);
        tree_dump_plot_tangent(out_tree, config, plt_comment);
    }

    return OK;
}

static err_t tree_derivative_sequence_with_dump(tree_t*              in_tree,
                                                tree_t*              out_tree,
                                                derivative_config_t* config,
                                                size_t               var_hash)
{
    if (!in_tree || !out_tree || !config)
        return ERR_BAD_ARG;

    size_t n = config->derivative_n;

    char   variable_str[2] = { config->variable, '\0' };
    double tangent_x       = config->tangent_x;

    tree_t current;
    err_t err = tree_ctor(&current);
    if (err != OK)
        return err;

    err = tree_clone_into(in_tree, &current);
    if (err != OK)
    {
        tree_clear(&current);
        return err;
    }

    tree_dump_random_meme(config);
    tree_dump_latex(&current, config, "Начальная запись решения:");

    for (size_t i = 0; i < n; ++i)
    {
        tree_clear(out_tree);

        err = tree_derivative_once(&current, out_tree, var_hash);
        if (err != OK)
        {
            tree_clear(&current);
            tree_clear(out_tree);
            return err;
        }

        tree_dump_random_meme(config);

        char comment[256] = {0};
        snprintf(comment, sizeof(comment),
                 "Производная %zu-го порядка по %s:",
                 i + 1, variable_str);
        tree_dump_latex(out_tree, config, comment);

        if (config->plot_derivative)
        {
            char plot_comment[256] = {0};
            snprintf(plot_comment, sizeof(plot_comment),
                     "График производной %zu-го порядка по %s:",
                     i + 1, variable_str);
            tree_dump_plot(out_tree, config, plot_comment);
        }


        tree_optimize(out_tree);

        char opt_comment[256] = {0};
        snprintf(opt_comment, sizeof(opt_comment),
                 "Производная %zu-го порядка по %s (после оптимизации):",
                 i + 1, variable_str);
        tree_dump_latex(out_tree, config, opt_comment);

        if (config->plot_tangent_derivative)
        {
            char plt_comment[256] = {0};
            snprintf(plt_comment, sizeof(plt_comment),
                     "График касательной к производной %zu-го порядка по %s в точке %lg:",
                     i + 1, variable_str, tangent_x);
            tree_dump_plot_tangent(out_tree, config, plt_comment);
        }

        err = tree_clone_into(out_tree, &current);
        if (err != OK)
        {
            tree_clear(&current);
            tree_clear(out_tree);
        return err;
        }
    }

    tree_clear(&current);
    return OK;
}

static err_t tree_derivative_with_dump_body(tree_t*              in_tree,
                                            tree_t*              out_tree,
                                            derivative_config_t* config,
                                            size_t               var_hash)
{
    if (!in_tree || !out_tree || !config)
        return ERR_BAD_ARG;

    size_t n = config->derivative_n;

    dump_initial_function_and_plots(in_tree, config);

    if (n == 0)
        return tree_derivative_zero_with_dump(in_tree, out_tree, config);

    return tree_derivative_sequence_with_dump(in_tree, out_tree,
                                              config, var_hash);
}

err_t tree_derivative(tree_t*              in_tree,
                      tree_t*              out_tree,
                      derivative_config_t* config)
{
    if (!in_tree || !out_tree || !config)
        return ERR_BAD_ARG;

    char   variable_str[2] = { config->variable, '\0' };
    size_t var_hash        = sdbm(variable_str);

    if (!config->dump_filename)
    {
        return tree_derivative_plain(in_tree, out_tree,
                                     var_hash, config->derivative_n);
    }

    tree_dump_reset(config->dump_filename);
    tree_dump_begin(config);

    err_t err = tree_derivative_with_dump_body(in_tree, out_tree,
                                               config, var_hash);

    tree_dump_end(config);
    return err;
}
*/
err_t tree_derivative_n(tree_t* in_tree,
                        tree_t* out_tree,
                        const char* var_name,
                        size_t derivative_n)
{
    if (!in_tree || !out_tree || !var_name)
        return ERR_BAD_ARG;

    const size_t var_hash = sdbm(var_name);
    err_t err = tree_derivative_plain(in_tree, out_tree, var_hash, derivative_n);
    if (err != OK)
        log_printf(ERROR, "tree_derivative_n: failed (var=%s, n=%zu)", var_name, derivative_n);

    return err;
}

#undef cL
#undef cR
#undef dL
#undef dR
#undef CONST_
#undef ADD_
#undef SUB_
#undef MUL_
#undef DIV_
#undef POW_
#undef SIN_
#undef COS_
#undef TAN_
#undef COT_
#undef SINH_
#undef COSH_
#undef TANH_
#undef COTH_
#undef LN_
#undef SQRT_

