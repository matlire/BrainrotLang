#include "middleend.h"

#include <math.h>
#include <stdio.h>

static inline int is_num_lit_(const ast_node_t* n)
{
    return n && n->kind == ASTK_NUM_LIT &&
           (n->u.num.lit_type == LIT_INT || n->u.num.lit_type == LIT_FLOAT);
}

static inline double as_f64_(const ast_node_t* n)
{
    return (n->u.num.lit_type == LIT_FLOAT) ? (double)n->u.num.lit.f64
                                            : (double)n->u.num.lit.i64;
}

static inline i64_t as_i64_(const ast_node_t* n)
{
    return (n->u.num.lit_type == LIT_FLOAT) ? (i64_t)n->u.num.lit.f64
                                            : (i64_t)n->u.num.lit.i64;
}

static int is_zero_(const ast_node_t* n)
{
    if (!is_num_lit_(n)) return 0;
    return (n->u.num.lit_type == LIT_FLOAT) ? (n->u.num.lit.f64 == 0.0)
                                            : (n->u.num.lit.i64 == 0);
}

static int is_one_(const ast_node_t* n)
{
    if (!is_num_lit_(n)) return 0;
    return (n->u.num.lit_type == LIT_FLOAT) ? (n->u.num.lit.f64 == 1.0)
                                            : (n->u.num.lit.i64 == 1);
}

static int truthy_(const ast_node_t* n)
{
    if (!is_num_lit_(n)) return 0;
    return (n->u.num.lit_type == LIT_FLOAT) ? (n->u.num.lit.f64 != 0.0)
                                            : (n->u.num.lit.i64 != 0);
}

static void make_num_int_(ast_node_t* n, i64_t v)
{
    n->kind = ASTK_NUM_LIT;
    n->type = AST_TYPE_INT;
    n->u.num.lit_type = LIT_INT;
    n->u.num.lit.i64 = v;
    n->left = NULL;
}

static void make_num_float_(ast_node_t* n, double v)
{
    n->kind = ASTK_NUM_LIT;
    n->type = AST_TYPE_FLOAT;
    n->u.num.lit_type = LIT_FLOAT;
    n->u.num.lit.f64 = (f64_t)v;
    n->left = NULL;
}

static int ipow_ok_(i64_t base, i64_t exp, i64_t* out)
{
    if (!out) return 0;
    if (exp < 0) return 0;

    i64_t res = 1;
    i64_t b   = base;
    i64_t e   = exp;

    while (e > 0)
    {
        if (e & 1) res *= b;
        e >>= 1;
        if (e) b *= b;
    }

    *out = res;
    return 1;
}

static inline ast_node_t* child_(ast_node_t* n, size_t idx)
{
    return ast_child(n, idx);
}

static ast_node_t* replace_with_(ast_node_t* node, ast_node_t* repl)
{
    if (!node || !repl) return node;

    ast_node_t* sib = node->right;

    repl->right = sib;

    return repl;
}

#define OPT_MARK_CHANGED_() block_begin *changed = 1; block_end
#define OPT_RETURN_(x)      block_begin return (x);   block_end

#define OPT_DONE_RET_N_()   block_begin OPT_MARK_CHANGED_(); OPT_RETURN_(n); block_end

#define OPT_MAKE_NUM_FI_(float_expr, int_expr)                   \
    block_begin                                                  \
        if (any_float) make_num_float_(n, (double)(float_expr)); \
        else           make_num_int_(n,   (i64_t)(int_expr));    \
    block_end

#define OPT_UNARY_COPY_(a_)                    \
    block_begin                                \
        if ((a_)->u.num.lit_type == LIT_FLOAT) \
            make_num_float_(n, as_f64_(a_));   \
        else                                   \
            make_num_int_(n, as_i64_(a_));     \
        OPT_DONE_RET_N_();                     \
    block_end

#define OPT_UNARY_NEG_(a_)                     \
    block_begin                                \
        if ((a_)->u.num.lit_type == LIT_FLOAT) \
            make_num_float_(n, -as_f64_(a_));  \
        else                                   \
            make_num_int_(n, -as_i64_(a_));    \
        OPT_DONE_RET_N_();                     \
    block_end

#define OPT_UNARY_NOT_(a_)                     \
    block_begin                                \
        make_num_int_(n, truthy_(a_) ? 0 : 1); \
        OPT_DONE_RET_N_();                     \
    block_end

#define OPT_BIN_TO_INT_(int_expr)            \
    block_begin                              \
        make_num_int_(n, (i64_t)(int_expr)); \
        OPT_DONE_RET_N_();                   \
    block_end

#define OPT_BIN_ARITH_(float_expr, int_expr)        \
    block_begin                                     \
        OPT_MAKE_NUM_FI_((float_expr), (int_expr)); \
        OPT_DONE_RET_N_();                          \
    block_end

#define OPT_BIN_DIV_()                           \
    block_begin                                  \
        if (any_float) {                         \
            const double rv = as_f64_(r);        \
            if (rv == 0.0) OPT_RETURN_(n);       \
            make_num_float_(n, as_f64_(l) / rv); \
        } else {                                 \
            const i64_t rv = as_i64_(r);         \
            if (rv == 0) OPT_RETURN_(n);         \
            make_num_int_(n, as_i64_(l) / rv);   \
        }                                        \
        OPT_DONE_RET_N_();                       \
    block_end

#define OPT_BIN_POW_()                                    \
    block_begin                                           \
        if (!any_float && r->u.num.lit_type == LIT_INT) { \
            i64_t out = 0;                                \
            if (ipow_ok_(as_i64_(l), as_i64_(r), &out)) { \
                make_num_int_(n, out);                    \
                OPT_DONE_RET_N_();                        \
            }                                             \
        }                                                 \
        make_num_float_(n, pow(as_f64_(l), as_f64_(r)));  \
        OPT_DONE_RET_N_();                                \
    block_end

static ast_node_t* optimize_chain_(ast_node_t* head, ast_node_t* parent, int* changed);

static ast_node_t* optimize_one_(ast_node_t* n, ast_node_t* parent, int* changed)
{
    if (!n) return NULL;

    n->parent = parent;
    if (n->left)
        n->left = optimize_chain_(n->left, n, changed);
 
    if (n->kind == ASTK_UNARY)
    {
        ast_node_t* a = child_(n, 0);
        if (is_num_lit_(a))
        {
            switch (n->u.unary.op)
            {
                case TOK_OP_PLUS:  OPT_UNARY_COPY_(a);
                case TOK_OP_MINUS: OPT_UNARY_NEG_(a);
                case TOK_OP_NOT:   OPT_UNARY_NOT_(a);
                default:           return n;
            }
        }
        return n;
    }

    if (n->kind == ASTK_BUILTIN_UNARY)
    {
        ast_node_t* a = child_(n, 0);
        if (is_num_lit_(a))
        {
            const double x = as_f64_(a);

            switch (n->u.builtin_unary.id)
            {
                case AST_BUILTIN_FLOOR: make_num_float_(n, floor(x)); *changed = 1; return n;
                case AST_BUILTIN_CEIL:  make_num_float_(n, ceil(x));  *changed = 1; return n;
                case AST_BUILTIN_ROUND: make_num_float_(n, round(x)); *changed = 1; return n;

                case AST_BUILTIN_ITOF:
                    make_num_float_(n, (double)as_i64_(a));
                    *changed = 1;
                    return n;

                case AST_BUILTIN_FTOI:
                    make_num_int_(n, (i64_t)as_f64_(a));
                    *changed = 1;
                    return n;

                default:
                    return n;
            }
        }

        return n;
    }

    if (n->kind == ASTK_BINARY)
    {
        ast_node_t* l = child_(n, 0);
        ast_node_t* r = child_(n, 1);

        if (!l || !r) return n;

        switch (n->u.binary.op)
        {
            case TOK_OP_PLUS:
                // x + 0 => x
                if (is_zero_(r))
                {
                    *changed = 1;
                    return replace_with_(n, l);
                }
                // 0 + x => x
                if (is_zero_(l))
                {
                    *changed = 1;
                    return replace_with_(n, r);
                }
                break;

            case TOK_OP_MUL:
            {
                // x * 0 / 0 * x => 0
                if (is_zero_(l) || is_zero_(r))
                {
                    const int any_float = (is_num_lit_(l) && l->u.num.lit_type == LIT_FLOAT) ||
                                          (is_num_lit_(r) && r->u.num.lit_type == LIT_FLOAT);
                    if (any_float) make_num_float_(n, 0.0);
                    else           make_num_int_(n,   0);
                    *changed = 1;
                    return n;
                }

                // x * 1 => x
                if (is_one_(r))
                {
                    *changed = 1;
                    return replace_with_(n, l);
                }

                // 1 * x => x
                if (is_one_(l))
                {
                    *changed = 1;
                    return replace_with_(n, r);
                }
                break;
            }

            case TOK_OP_POW:
                // x ^ 0 => 1
                if (is_zero_(r))
                {
                    // if exponent was float literal 0.0 or base is float literal, choose float 1.0
                    const int want_float = (is_num_lit_(l) && l->u.num.lit_type == LIT_FLOAT) ||
                                           (is_num_lit_(r) && r->u.num.lit_type == LIT_FLOAT);
                    if (want_float) make_num_float_(n, 1.0);
                    else            make_num_int_(n,   1);
                    *changed = 1;
                    return n;
                }

                // x ^ 1 => x
                if (is_one_(r))
                {
                    *changed = 1;
                    return replace_with_(n, l);
                }

                // 1 ^ x => 1
                if (is_one_(l))
                {
                    const int want_float = (is_num_lit_(l) && l->u.num.lit_type == LIT_FLOAT) ||
                                           (is_num_lit_(r) && r->u.num.lit_type == LIT_FLOAT);
                    if (want_float) make_num_float_(n, 1.0);
                    else            make_num_int_(n,   1);
                    *changed = 1;
                    return n;
                }
                break;

            default:
                break;
        }

        // constant folding       
        if (is_num_lit_(l) && is_num_lit_(r))
        {
            const int any_float =
                (l->u.num.lit_type == LIT_FLOAT) ||
                (r->u.num.lit_type == LIT_FLOAT);

            switch (n->u.binary.op)
            {
                case TOK_OP_OR:   OPT_BIN_TO_INT_((truthy_(l) || truthy_(r)) ? 1 : 0);
                case TOK_OP_AND:  OPT_BIN_TO_INT_((truthy_(l) && truthy_(r)) ? 1 : 0);

                case TOK_OP_EQ:   OPT_BIN_TO_INT_((as_f64_(l) == as_f64_(r)) ? 1 : 0);
                case TOK_OP_NEQ:  OPT_BIN_TO_INT_((as_f64_(l) != as_f64_(r)) ? 1 : 0);
                case TOK_OP_GT:   OPT_BIN_TO_INT_((as_f64_(l) >  as_f64_(r)) ? 1 : 0);
                case TOK_OP_LT:   OPT_BIN_TO_INT_((as_f64_(l) <  as_f64_(r)) ? 1 : 0);
                case TOK_OP_GTE:  OPT_BIN_TO_INT_((as_f64_(l) >= as_f64_(r)) ? 1 : 0);
                case TOK_OP_LTE:  OPT_BIN_TO_INT_((as_f64_(l) <= as_f64_(r)) ? 1 : 0);

                case TOK_OP_PLUS:  OPT_BIN_ARITH_(as_f64_(l) + as_f64_(r), as_i64_(l) + as_i64_(r));
                case TOK_OP_MINUS: OPT_BIN_ARITH_(as_f64_(l) - as_f64_(r), as_i64_(l) - as_i64_(r));
                case TOK_OP_MUL:   OPT_BIN_ARITH_(as_f64_(l) * as_f64_(r), as_i64_(l) * as_i64_(r));

                case TOK_OP_DIV:   OPT_BIN_DIV_();
                case TOK_OP_POW:   OPT_BIN_POW_();

                default:
                    return n;
            }
        }
        return n;
    }

    return n;
}

static ast_node_t* optimize_chain_(ast_node_t* head, ast_node_t* parent, int* changed)
{
    if (!head) return NULL;
    ast_node_t* cur = optimize_one_(head, parent, changed);

    if (cur)
        cur->right = optimize_chain_(cur->right, parent, changed);

    return cur;
}

err_t ast_optimize(ast_tree_t* tree, int* out_changed)
{
    if (!tree || !out_changed) return ERR_BAD_ARG;

    int changed = 0;
    tree->root = optimize_chain_(tree->root, NULL, &changed);
    if (tree->root) tree->root->parent = NULL;

    *out_changed = changed;
    return OK;
}
