#include "diff-tree.h"

#include <math.h>

static int is_num(const node_t* n, double v)
{
    return n &&
           n->node_type == TYPE_NUM &&
           is_same(n->value.d_value, v);
}

#define L node->left
#define R node->right

static node_t* optimize_add_node(node_t* node, int* changed)
{
    if (is_num(L, 0.0) && !is_num(R, 0.0))
    {
        node_t* res = R;
        tree_delete_node(L, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    if (is_num(R, 0.0) && !is_num(L, 0.0))
    {
        node_t* res = L;
        tree_delete_node(R, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    if (is_num(L, 0.0) && is_num(R, 0.0))
    {
        tree_delete_node(L, 0);
        tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = 0.0;
        *changed = 1;
        return node;
    }

    return node;
}

static node_t* optimize_sub_node(node_t* node, int* changed)
{
    if (L && R &&
        L->node_type == TYPE_NUM &&
        R->node_type == TYPE_NUM)
    {
        double res_val = L->value.d_value - R->value.d_value;
        tree_delete_node(L, 0);
        tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = res_val;
        *changed = 1;
        return node;
    }

    if (is_num(R, 0.0) && !is_num(L, 0.0))
    {
        node_t* res = L;
        tree_delete_node(R, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    return node;
}

static int fold_const_pow_subtree(const node_t* n, double* out)
{
    if (!n || !out)
        return 0;

    switch (n->node_type)
    {
        case TYPE_NUM:
            *out = n->value.d_value;
            return 1;

        case TYPE_VAR:
            return 0;

        case TYPE_OP:
        {
            double lv = 0.0;
            double rv = 0.0;

            switch (n->value.op)
            {
                case OP_ADD:
                    if (!fold_const_pow_subtree(n->left,  &lv) ||
                        !fold_const_pow_subtree(n->right, &rv))
                        return 0;
                    *out = lv + rv;
                    return 1;

                case OP_SUB:
                    if (!fold_const_pow_subtree(n->left,  &lv) ||
                        !fold_const_pow_subtree(n->right, &rv))
                        return 0;
                    *out = lv - rv;
                    return 1;

                case OP_MUL:
                    if (!fold_const_pow_subtree(n->left,  &lv) ||
                        !fold_const_pow_subtree(n->right, &rv))
                        return 0;
                    *out = lv * rv;
                    return 1;

                case OP_DIV:
                    if (!fold_const_pow_subtree(n->left,  &lv) ||
                        !fold_const_pow_subtree(n->right, &rv))
                        return 0;
                    if (is_same(rv, 0.0))
                        return 0;
                    *out = lv / rv;
                    return 1;

                default:
                    return 0;
            }
        }

        default:
            return 0;
    }
}

static node_t* optimize_mul_node(node_t* node, int* changed)
{
    if (is_num(L, 0.0) || is_num(R, 0.0))
    {
        if (L) tree_delete_node(L, 0);
        if (R) tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = 0.0;
        *changed = 1;
        return node;
    }

    if (is_num(L, 1.0) && !is_num(R, 1.0))
    {
        node_t* res = R;
        tree_delete_node(L, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    if (is_num(R, 1.0) && !is_num(L, 1.0))
    {
        node_t* res = L;
        tree_delete_node(R, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    if (is_num(L, 1.0) && is_num(R, 1.0))
    {
        tree_delete_node(L, 0);
        tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = 1.0;
        *changed = 1;
        return node;
    }

    return node;
}

static node_t* optimize_div_node(node_t* node, int* changed)
{
    if (L && R &&
        L->node_type == TYPE_NUM && R->node_type == TYPE_NUM)
    {
        if (!is_same(R->value.d_value, 0.0))
        {
            double res_val = L->value.d_value / R->value.d_value;
            tree_delete_node(L, 0);
            tree_delete_node(R, 0);
            L = R = NULL;
            node->node_type     = TYPE_NUM;
            node->value.d_value = res_val;
            *changed = 1;
            return node;
        }
    }
    if (is_num(L, 0.0) && !is_num(R, 0.0))
    {
        if (!is_num(R, 0.0))
        {
            if (R) tree_delete_node(R, 0);
            if (L) tree_delete_node(L, 0);
            L = R = NULL;
            node->node_type     = TYPE_NUM;
            node->value.d_value = 0.0;
            *changed = 1;
            return node;
        }
    }
    if (is_num(R, 1.0) && !is_num(L, 1.0))
    {
        node_t* res = L;
        tree_delete_node(R, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }
    return node;
}

static node_t* optimize_pow_node(node_t* node, int* changed)
{
    if (is_num(R, 0.0))
    {
        if (L) tree_delete_node(L, 0);
        if (R) tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = 1.0;
        *changed = 1;
        return node;
    }

    if (is_num(R, 1.0))
    {
        node_t* res = L;
        tree_delete_node(R, 0);
        node->left  = NULL;
        node->right = NULL;
        node_dtor(node);
        *changed = 1;
        return res;
    }

    if (is_num(L, 1.0))
    {
        if (L) tree_delete_node(L, 0);
        if (R) tree_delete_node(R, 0);
        L = R = NULL;
        node->node_type     = TYPE_NUM;
        node->value.d_value = 1.0;
        *changed = 1;
        return node;
    }

    if (R)
    {
        double exp_val = 0.0;
        if (fold_const_pow_subtree(R, &exp_val))
        {
            if (R->left)
            {
                tree_delete_node(R->left, 0);
                R->left = NULL;
            }
            if (R->right)
            {
                tree_delete_node(R->right, 0);
                R->right = NULL;
            }
            R->node_type     = TYPE_NUM;
            R->value.d_value = exp_val;
            *changed = 1;
            return node;
        }
    }

    return node;
}

static node_t* optimize_subtree(node_t* node, int* changed)
{
    if (!node) return NULL;

    if (L)
        L = optimize_subtree(L, changed);
    if (R)
        R = optimize_subtree(R, changed);

    if (node->node_type != TYPE_OP)
        return node;

    switch (node->value.op)
    {
        case OP_ADD:
            return optimize_add_node(node, changed);
        case OP_SUB:
            return optimize_sub_node(node, changed);
        case OP_MUL:
            return optimize_mul_node(node, changed);
        case OP_DIV:
            return optimize_div_node(node, changed);
        case OP_POW:
            return optimize_pow_node(node, changed);
        default:
            return node;
    }
}

#undef L
#undef R

void tree_optimize(tree_t* tree)
{
    if (!tree)
        return;

    int changed = 0;
    tree->root = optimize_subtree(tree->root, &changed);
}

