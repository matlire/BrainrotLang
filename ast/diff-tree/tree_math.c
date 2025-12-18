#include "diff-tree.h"

#include <math.h>
#include <stdio.h>

static double eval_node(tree_t* tree, node_t* node)
{
    if (!node)
        return NAN;

    switch (node->node_type)
    {
        case TYPE_NUM:
            return node->value.d_value;

        case TYPE_VAR:
        {
            var_t* v = get_or_create_var(tree, node->value.var.name);
            if (!v)
                return NAN;

            if (isnan(v->value))
            {
                printf("Enter value for variable %s: ",
                       v->name ? v->name : "?");
                fflush(stdout);
                if (scanf("%lf", &v->value) != 1)
                    return NAN;
            }

            return v->value;
        }

        case TYPE_OP:
        {
            double left  = node->left  ? eval_node(tree, node->left)  : NAN;
            double right = node->right ? eval_node(tree, node->right) : NAN;

            switch (node->value.op)
            {
                case OP_ADD:  return left + right;
                case OP_SUB:  return left - right;
                case OP_MUL:  return left * right;
                case OP_DIV:  return left / right;
                case OP_POW:  return pow(left, right);

                case OP_SIN:  return sin(left);
                case OP_COS:  return cos(left);
                case OP_TAN:  return tan(left);
                case OP_COT:  return 1.0 / tan(left);

                case OP_SINH: return sinh(left);
                case OP_COSH: return cosh(left);
                case OP_TANH: return tanh(left);
                case OP_COTH: return cosh(left)/sinh(left);

                case OP_LOG:  return log(right) / log(left);
                case OP_LN:   return log(left);
                case OP_SQRT: return sqrt(left);

                case OP_ASIN: return asin(left);
                case OP_ACOS: return acos(left);
                case OP_ATAN: return atan(left);
                case OP_ACOT: return atan(1.0 / left);

                case OP_NOP:  return left;

                default:      return NAN;
            }
        }

        default:
            return NAN;
    }
}

double evaluate_tree(tree_t* tree)
{
    if (!tree || !tree->root)
        return NAN;

    return eval_node(tree, tree->root);
}
