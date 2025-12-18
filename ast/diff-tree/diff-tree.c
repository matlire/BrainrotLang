#include "diff-tree.h"

#include <ctype.h>
#include <math.h>

const double FLT_ERR = 1e-6;

#define DECLARE_OP_DESC(name, text, id) { text, 0, name },

static op_t g_op_desc[] = {
    OPERATION_LIST(DECLARE_OP_DESC)
};

static int g_op_hashes_inited = 0;

static void init_op_hashes(void)
{
    if (g_op_hashes_inited)
        return;
    size_t n = sizeof(g_op_desc) / sizeof(g_op_desc[0]);
    for (size_t i = 0; i < n; ++i)
        g_op_desc[i].hash = sdbm(g_op_desc[i].str);
    g_op_hashes_inited = 1;
}

static int op_from_token_hash(const char* tok, node_operations_e* out_op)
{
    init_op_hashes();
    size_t h = sdbm(tok);
    size_t n = sizeof(g_op_desc) / sizeof(g_op_desc[0]);
    for (size_t i = 0; i < n; ++i)
    {
        if (g_op_desc[i].hash == h)
        {
            if (out_op)
                *out_op = g_op_desc[i].op;
            return 1;
        }
    }
    return 0;
}

static char* ltrim(char* s)
{
    while (isspace((unsigned char)*s))
        s++;
    return s;
}

static char* rtrim(char* s)
{
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) 
        *--end = '\0';
    return s;
}

static char* trim(char* s)
{
    return rtrim(ltrim(s));
}

static void skip_ws_(const char** s)
{
    while (s && *s && isspace((unsigned char)**s))
        (*s)++;
}

bool is_same(double a, double b) 
{
    return fabs(a - b) < FLT_ERR;
}

static err_t vars_ensure_capacity(tree_t *tree, size_t min_capacity)
{
    if (!tree)
        return ERR_BAD_ARG;

    size_t new_cap = (tree->var_capacity > 0) ? (tree->var_capacity * 2) : VARS_MIN_CAP;
    if (new_cap < min_capacity)
        new_cap = min_capacity;

    var_t *new_arr = (var_t*)realloc(tree->variables, new_cap * sizeof(var_t));
    if (!new_arr)
        return ERR_ALLOC;

    if (new_cap > tree->var_capacity)
    {
        size_t old_cap = tree->var_capacity;
        memset(new_arr + old_cap, 0, (new_cap - old_cap) * sizeof(var_t));
    }

    tree->variables    = new_arr;
    tree->var_capacity = new_cap;

    return OK;
}

static void vars_free(tree_t *tree)
{
    if (!tree)
        return;

    if (tree->variables)
    {
        for (size_t i = 0; i < tree->var_amount; ++i)
        {
            if (tree->variables[i].name)
            {
                free(tree->variables[i].name);
                tree->variables[i].name = NULL;
            }
        }

        free(tree->variables);
        tree->variables = NULL;
    }

    tree->var_amount   = 0;
    tree->var_capacity = 0;
}

err_t node_ctor(node_t** node)
{
    if (!CHECK(ERROR, node != NULL, "node_ctor: node is NULL"))
        return ERR_BAD_ARG;

    *node = (node_t*)calloc(1, sizeof(**node));
    if (*node == NULL)
        return ERR_ALLOC;

    (*node)->node_type     = TYPE_NUM;
    (*node)->value.d_value = 0.0;
    (*node)->left          = NULL;
    (*node)->right         = NULL;
    (*node)->rank          = 0;

    return OK;
}

err_t node_dtor(node_t* node)
{
    if (!CHECK(ERROR, node != NULL, "node_dtor: node is NULL"))
        return ERR_BAD_ARG;

    if (node->node_type == TYPE_VAR && node->value.var.name != NULL)
    {
        free(node->value.var.name);
        node->value.var.name = NULL;
    }

    free(node);

    return OK;
}

err_t tree_ctor(tree_t * const tree)
{
    if (!CHECK(ERROR, tree != NULL, "tree_ctor: tree is NULL"))
        return ERR_BAD_ARG;

    tree->nodes_amount = 0;
    tree->root         = NULL;
    tree->variables    = NULL;
    tree->var_amount   = 0;
    tree->var_capacity = 0;
    return OK;
}

err_t tree_dtor(tree_t * const tree)
{
    if (!CHECK(ERROR, tree != NULL, "tree_dtor: tree is NULL"))
        return ERR_BAD_ARG;

    return tree_clear(tree);
}

err_t tree_verify(const tree_t * const tree)
{
    if (!CHECK(ERROR, tree != NULL, "tree_verify: tree is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, tree->nodes_amount < MAX_RECURSION_LIMIT,
               "tree_verify: tree->nodes_amount too large"))
        return ERR_CORRUPT;

    return OK;
}

const char* op_to_str(node_operations_e op)
{
    switch (op)
    {
        case OP_ADD:  return "+";
        case OP_SUB:  return "-";
        case OP_MUL:  return "*";
        case OP_DIV:  return "/";
        case OP_POW:  return "^";

        case OP_SIN:  return "sin";
        case OP_COS:  return "cos";
        case OP_TAN:  return "tan";
        case OP_COT:  return "cot";

        case OP_SINH: return "sinh";
        case OP_COSH: return "cosh";
        case OP_TANH: return "tanh";
        case OP_COTH: return "coth";

        case OP_LOG:  return "log";
        case OP_LN:   return "ln";
        case OP_SQRT: return "sqrt";

        case OP_ASIN: return "asin";
        case OP_ACOS: return "acos";
        case OP_ATAN: return "atan";
        case OP_ACOT: return "acot";

        case OP_NOP:
        default:      return "?";
    }
}

err_t tree_delete_node(node_t * node, size_t iter)
{
    if (!CHECK(ERROR, node != NULL, "tree_delete_node: node is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, iter <= MAX_RECURSION_LIMIT,
               "tree_delete_node: recursion limit exceeded on node %p", node))
        return ERR_CORRUPT;

    if (node->left != NULL)
        if (!CHECK(ERROR, tree_delete_node(node->left, iter + 1) == OK,
                   "tree_delete_node: failed to delete left node"))
            return ERR_CORRUPT;

    if (node->right != NULL)
        if (!CHECK(ERROR, tree_delete_node(node->right, iter + 1) == OK,
                   "tree_delete_node: failed to delete right node"))
            return ERR_CORRUPT;

    node_dtor(node);

    return OK;
}

err_t tree_clear(tree_t * const tree)
{
    if (!CHECK(ERROR, tree != NULL, "tree_clear: tree is NULL"))
        return ERR_BAD_ARG;

    if (tree->root != NULL)
    {
        if (!CHECK(ERROR, tree_delete_node(tree->root, 0) == OK,
                   "tree_clear: failed to delete nodes"))
            return ERR_CORRUPT;
    }

    tree->root         = NULL;
    tree->nodes_amount = 0;

    vars_free(tree);

    return OK;
}

err_t tree_insert(tree_t * const tree, node_t * const node)
{
    if (!CHECK(ERROR, tree != NULL, "tree_insert: tree is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, node != NULL, "tree_insert: node is NULL"))
        return ERR_BAD_ARG;

    tree->nodes_amount++;
    if (tree->root == NULL)
        tree->root = node;

    return OK;
}

var_t* get_or_create_var(tree_t* tree, const char* name)
{
    size_t h = sdbm(name);
    for (size_t i = 0; i < tree->var_amount; ++i)
    {
        if (tree->variables[i].hash == h)
            return &tree->variables[i];
    }

    if (vars_ensure_capacity(tree, tree->var_amount + 1) != OK)
        return NULL;

    var_t* v = &tree->variables[tree->var_amount++];
    v->name  = strdup(name);
    v->hash  = h;
    v->value = NAN;

    return v;
}

size_t get_op_rank(node_operations_e op)
{
    switch (op)
    {
        case OP_ADD:
        case OP_SUB:  return 10;

        case OP_MUL:
        case OP_DIV:  return 20;

        case OP_POW:  return 30;

        case OP_SIN:
        case OP_COS:
        case OP_TAN:
        case OP_COT:
        case OP_SINH:
        case OP_COSH:
        case OP_TANH:
        case OP_COTH:
        case OP_LOG:
        case OP_LN:
        case OP_SQRT:
        case OP_ASIN:
        case OP_ACOS:
        case OP_ATAN:
        case OP_ACOT: return 40;

        case OP_NOP:
        default:      return 0;
    }
}

static node_t* new_num_node(tree_t* tree, double val, err_t* err)
{
    if (!tree || !err || *err != OK)
        return NULL;

    node_t* n = NULL;
    *err = node_ctor(&n);
    if (*err != OK || !n)
    {
        *err = ERR_ALLOC;
        return NULL;
    }

    n->node_type     = TYPE_NUM;
    n->value.d_value = val;
    n->left          = NULL;
    n->right         = NULL;
    n->rank          = 100;

    tree->nodes_amount++;
    return n;
}

static node_t* new_op_node(tree_t* tree,
                           node_operations_e op,
                           node_t* left,
                           node_t* right,
                           err_t*  err)
{
    if (!tree || !err || *err != OK)
        return NULL;

    node_t* n = NULL;
    *err = node_ctor(&n);
    if (*err != OK || !n)
    {
        *err = ERR_ALLOC;
        return NULL;
    }

    n->node_type = TYPE_OP;
    n->value.op  = op;
    n->left      = left;
    n->right     = right;
    n->rank      = get_op_rank(op);

    tree->nodes_amount++;
    return n;
}

static node_t* new_var_node(tree_t* tree, const char* name, err_t* err)
{
    if (!tree || !err || *err != OK)
        return NULL;

    var_t* v = get_or_create_var(tree, name);
    if (!v)
    {
        *err = ERR_ALLOC;
        return NULL;
    }

    node_t* n = NULL;
    *err = node_ctor(&n);
    if (*err != OK || !n)
    {
        *err = ERR_ALLOC;
        return NULL;
    }

    n->node_type       = TYPE_VAR;
    n->value.var.name  = strdup(v->name);
    n->value.var.hash  = v->hash;
    n->value.var.value = v->value;
    n->left            = NULL;
    n->right           = NULL;
    n->rank            = 100;

    tree->nodes_amount++;
    return n;
}

static int is_ident_start(unsigned char c)
{
    return isalpha(c);
}

static int is_ident_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

static node_t* GetN(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    const char* start = *s;
    char* end = NULL;

    double val = strtod(start, &end);

    if (end == start) 
    {
        *err = ERR_SYNTAX;
        return NULL;
    }

    *s = end;

    return new_num_node(tree, val, err);
}

static node_t* GetE(const char** s, tree_t* tree, err_t* err);

static node_t* GetP(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    if (**s == '(')
    {
        (*s)++;
        skip_ws_(s);
        node_t* val = GetE(s, tree, err);
        if (*err != OK || !val)
            return NULL;
        skip_ws_(s);
        if (**s != ')')
        {
            *err = ERR_SYNTAX;
            return NULL;
        }
        (*s)++;
        return val;
    }

    if (is_ident_start((unsigned char)**s))
    {
        char ident[32] = {0};
        size_t n = 0;

        while (is_ident_char((unsigned char)**s) && n + 1 < sizeof(ident))
        {
            ident[n++] = **s;
            (*s)++;
        }
        ident[n] = '\0';

        node_operations_e op = OP_NOP;
        if (op_from_token_hash(ident, &op))
        {
            if (**s != '(')
            {
                *err = ERR_SYNTAX;
                return NULL;
            }

            (*s)++;
            skip_ws_(s);
            node_t* arg1 = GetE(s, tree, err);
            if (*err != OK || !arg1)
                return NULL;
            skip_ws_(s);
            if (**s != ')')
            {
                *err = ERR_SYNTAX;
                return NULL;
            }
            (*s)++;

            if (op == OP_LOG && **s == '(')
            {
                (*s)++;
                skip_ws_(s);
                node_t* arg2 = GetE(s, tree, err);
                if (*err != OK || !arg2)
                    return NULL;
                skip_ws_(s);
                if (**s != ')')
                {
                    *err = ERR_SYNTAX;
                    return NULL;
                }
                (*s)++;

                return new_op_node(tree, OP_LOG, arg1, arg2, err);
            }

            return new_op_node(tree, op, arg1, NULL, err);
        }
        else
        {
            if (n == 1 &&
                (('a' <= ident[0] && ident[0] <= 'z') ||
                 ('A' <= ident[0] && ident[0] <= 'Z')))
            {
                return new_var_node(tree, ident, err);
            }

            *err = ERR_SYNTAX;
            return NULL;
        }
    }

    return GetN(s, tree, err);
}

static node_t* GetPW(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    node_t* val = GetP(s, tree, err);
    skip_ws_(s);

    while (**s == '^')
    {
        (*s)++;
        skip_ws_(s);
        node_t* rhs = GetP(s, tree, err);
        skip_ws_(s);
        val = new_op_node(tree, OP_POW, val, rhs, err);
    }

    return val;
}

static node_t* GetT(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    node_t* val = GetPW(s, tree, err);
    skip_ws_(s);
    while (**s == '*' || **s == '/')
    {
        const char op = **s;
        (*s)++;
        skip_ws_(s);
        node_t* val2 = GetPW(s, tree, err);
        skip_ws_(s);

        if (op == '*')
            val = new_op_node(tree, OP_MUL, val, val2, err);
        else
            val = new_op_node(tree, OP_DIV, val, val2, err);
    }

    return val;
}

static node_t* GetE(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    node_t* val = GetT(s, tree, err);
    skip_ws_(s);
    while (**s == '+' || **s == '-')
    {
        const char op = **s;
        (*s)++;
        skip_ws_(s);
        node_t* val2 = GetT(s, tree, err);
        skip_ws_(s);

        if (op == '+')
            val = new_op_node(tree, OP_ADD, val, val2, err);
        else
            val = new_op_node(tree, OP_SUB, val, val2, err);
    }

    return val;
}

static node_t* GetG(const char** s, tree_t* tree, err_t* err)
{
    skip_ws_(s);
    node_t* val = GetE(s, tree, err);
    if (*err != OK || !val)
        return NULL;

    skip_ws_(s);
    if (!(**s == '\0' || **s == '\n' || **s == '\r'))
    {
        *err = ERR_SYNTAX;
        return NULL;
    }

    return val;
}

err_t tree_parse_expr(tree_t* tree, const char* expr)
{
    if (!tree || !expr)
        return ERR_BAD_ARG;

    err_t err = tree_clear(tree);
    if (err != OK)
        return err;

    err_t parse_err = OK;
    const char* p = expr;

    tree->root = GetG(&p, tree, &parse_err);

    if (tree->root == NULL || parse_err != OK)
    {
        log_printf(ERROR, "Parse error in expression: %s", expr);
        tree_clear(tree);
        return (parse_err != OK) ? parse_err : ERR_SYNTAX;
    }

    return OK;
}

static void derivative_config_init_defaults(derivative_config_t* config)
{
    if (!config)
        return;

    config->dump_filename           = NULL;
    config->derivative_n            = 0;
    config->variable                = 'x';
    config->x_from                  = 0.0;
    config->x_to                    = 0.0;
    config->y_from                  = 0.0;
    config->y_to                    = 0.0;
    config->step                    = 0.0;
    config->tangent_x               = 0.0;
    config->plot_original           = false;
    config->plot_tangent_original   = false;
    config->plot_derivative         = false;
    config->plot_tangent_derivative = false;
    config->plot_taylor             = false;
}

typedef enum 
{
    CFG_NOTYPE,
    CFG_DOUBLE,
    CFG_SIZE_T,
    CFG_BOOL,
    CFG_STRING,
} cfg_type_t;

typedef struct 
{
    const char* key;
    cfg_type_t  type;
    size_t      offset;
} cfg_entry_t;

#define CFG_DOUBLE_ENTRY(_key, _field) \
    { (_key), CFG_DOUBLE, offsetof(derivative_config_t, _field) }

#define CFG_SIZE_T_ENTRY(_key, _field) \
    { (_key), CFG_SIZE_T, offsetof(derivative_config_t, _field) }

#define CFG_BOOL_ENTRY(_key, _field) \
    { (_key), CFG_BOOL, offsetof(derivative_config_t, _field) }
    
#define CFG_STRING_ENTRY(_key, _field) \
    { (_key), CFG_STRING, offsetof(derivative_config_t, _field) }

static const cfg_entry_t cfg_table[] = {
    CFG_STRING_ENTRY("dump_filename",          dump_filename),
    CFG_SIZE_T_ENTRY("derivative_n",           derivative_n),
    CFG_SIZE_T_ENTRY("taylor_n",               taylor_n),

    CFG_DOUBLE_ENTRY("x_from",                 x_from),
    CFG_DOUBLE_ENTRY("x_to",                   x_to),
    CFG_DOUBLE_ENTRY("y_from",                 y_from),
    CFG_DOUBLE_ENTRY("y_to",                   y_to),
    CFG_DOUBLE_ENTRY("taylor_y_from",          taylor_y_from),
    CFG_DOUBLE_ENTRY("taylor_y_to",            taylor_y_to),
    CFG_DOUBLE_ENTRY("step",                   step),
    CFG_DOUBLE_ENTRY("tangent_x",              tangent_x),

    CFG_BOOL_ENTRY("plot_original",            plot_original),
    CFG_BOOL_ENTRY("plot_tangent_original",    plot_tangent_original),
    CFG_BOOL_ENTRY("plot_derivative",          plot_derivative),
    CFG_BOOL_ENTRY("plot_tangent_derivative",  plot_tangent_derivative),
    CFG_BOOL_ENTRY("plot_taylor",              plot_taylor),

    { NULL, CFG_NOTYPE, 0 }
};

static char* strip_quotes(char *v)
{
    if (*v == '"' || *v == '\'') {
        char quote = *v;
        v++;
        char *endq = strrchr(v, quote);
        if (endq)
            *endq = '\0';
    }
    return v;
}

static void cfg_assign_field(derivative_config_t* config,
                             const cfg_entry_t*   e,
                             char*                v)
{
    char* base = (char*)config;

    switch (e->type)
    {
        case CFG_DOUBLE:
            *(double*)(base + e->offset) = strtod(v, NULL);
            break;

        case CFG_SIZE_T:
            *(size_t*)(base + e->offset) = (size_t)strtoull(v, NULL, 10);
            break;

        case CFG_BOOL:
            *(bool*)(base + e->offset) = (strtol(v, NULL, 10) != 0);
            break;

        case CFG_STRING:
        {
            v = strip_quotes(v);
            char** dst = (char**)(base + e->offset);

            free(*dst);
            *dst = strdup(v);
            break;
        }

        default:
            break;
    }
}

static err_t derivative_config_parse_kv(derivative_config_t* config,
                                        const char*          key,
                                        char*                value)
{
    if (!config || !key || !value)
        return ERR_BAD_ARG;

    char *v = value;

    if (strcmp(key, "dump_filename") == 0)
    {
        v = strip_quotes(v);

        static char dump_buf[512];
        size_t len = strlen(v);
        if (len >= sizeof(dump_buf))
            len = sizeof(dump_buf) - 1;

        memcpy(dump_buf, v, len);
        dump_buf[len] = '\0';

        config->dump_filename = dump_buf;
        return OK;
    }
    else if (strcmp(key, "variable") == 0)
    {
        v = strip_quotes(v);
        config->variable = v[0];
        return OK;
    }

    for (const cfg_entry_t *e = cfg_table; e->key != NULL; ++e)
    {
        if (strcmp(key, e->key) == 0) {
            cfg_assign_field(config, e, v);
            return OK;
        }
    }

    return OK;
}

#define CLEANUP_AND_RETURN(code) \
    block_begin                  \
        free(op_data.buffer);    \
        if (file) fclose(file);  \
        tree_dtor(tree);         \
        return (code);           \
    block_end

static err_t parse_equation_string(tree_t*     tree,
                                   const char* expr,
                                   const char* filename)
{
    err_t parse_err = OK;
    const char *expr_ptr = expr;

    tree->root = GetG(&expr_ptr, tree, &parse_err);
    if (tree->root == NULL || parse_err != OK) 
    {
        log_printf(ERROR,
                   "Parse error in \"%s\"",
                   filename ? filename : "<input>");
        return ERR_CORRUPT;
    }

    return OK;
}

static char *trim_and_unquote(char *s)
{
    s = trim(s);

    if (*s == '"' || *s == '\'') 
    {
        char quote = *s;
        s++;
        size_t len = strlen(s);
        if (len > 0 && s[len - 1] == quote)
            s[len - 1] = '\0';
    }

    return s;
}

static size_t skip_utf8_bom(const char* buf, size_t size)
{
    if (size >= 3 &&
        (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) return 3;
    return 0;
}

static int buffer_has_equal(const char* buf)
{
    for (size_t i = 0; buf[i] != '\0'; ++i) 
    {
        if (buf[i] == '=')
            return 1;
    }
    return 0;
}

static err_t parse_config_and_equation(tree_t*              tree,
                                       derivative_config_t* config,
                                       char*                buf,
                                       const char*          filename)
{
    char *cursor = buf;
    int equation_parsed = 0;

    while (*cursor) {
        char *line_start = cursor;
        char *line_end   = cursor;

        while (*line_end && *line_end != '\n' && *line_end != '\r')
            line_end++;

        char saved = *line_end;
        *line_end  = '\0';

        char *line = trim(line_start);

        if (*line != '\0' &&
            *line != '#' &&
            !(line[0] == '/' && line[1] == '/'))
        {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';

                char *key   = trim(line);
                char *value = trim(eq + 1);

                if (strcmp(key, "equation") == 0) {
                    char *expr = trim_and_unquote(value);

                    err_t r = parse_equation_string(tree, expr, filename);
                    if (r != OK)
                        return r;

                    equation_parsed = 1;
                }
                else if (config) {
                    err_t cfg_err = derivative_config_parse_kv(config, key, value);
                    if (cfg_err != OK)
                        return cfg_err;
                }
            }
        }

        *line_end = saved;

        if (saved == '\r' && line_end[1] == '\n')
            line_end++;

        if (*line_end == '\0')
            break;

        cursor = line_end + 1;
    }

    if (!equation_parsed)
        return ERR_CORRUPT;

    return OK;
}

err_t tree_read_file(tree_t* tree,
                     derivative_config_t* config,
                     const char* filename,
                     logging_level level)
{
    if (!CHECK(ERROR, tree != NULL, "tree_read_file: tree == NULL")) 
        return ERR_BAD_ARG;
    unused level;

    ssize_t rsize = get_file_size_stat(filename) + 1;
    if (rsize == -1) 
        return ERR_CORRUPT;
    size_t fsize = (size_t)rsize;
    operational_data_t op_data = {  };
    op_data.buffer_size = fsize;
    op_data.buffer      = (char*)calloc(1, fsize);
    if (!op_data.buffer)
        return ERR_ALLOC;

    FILE* file  = load_file(filename, "rb");
    if (!file) CLEANUP_AND_RETURN(ERR_CORRUPT);

    size_t read = read_file(file, &op_data);
    if (read == 0) CLEANUP_AND_RETURN(ERR_CORRUPT);

    size_t bom_offset = skip_utf8_bom(op_data.buffer, op_data.buffer_size);
    char *buf = op_data.buffer + bom_offset;

    
    int has_equal = buffer_has_equal(buf);
    tree_clear(tree);

    if (!has_equal)
    {
        err_t r = parse_equation_string(tree, buf, filename);
        free(op_data.buffer);
        fclose(file);
        return r;
    }
 
    if (config)
        derivative_config_init_defaults(config);

    err_t r = parse_config_and_equation(tree, config, buf, filename);
    free(op_data.buffer);
    fclose(file);

    if (r != OK)
    {
        tree_clear(tree);
        return ERR_CORRUPT;
    }
    return OK;
}

node_t* clone_subtree(const node_t* src,
                      tree_t*       dst,
                      size_t        depth,
                      err_t*        err)
{
    if (!src || !dst || !err)
        return NULL;

    if (*err != OK)
        return NULL;

    if (depth > MAX_RECURSION_LIMIT)
    {
        *err = ERR_CORRUPT;
        return NULL;
    }

    node_t *node = NULL;
    err_t rc = node_ctor(&node);
    if (rc != OK)
    {
        *err = rc;
        return NULL;
    }

    node->node_type = src->node_type;
    node->left      = NULL;
    node->right     = NULL;

    if (src->node_type == TYPE_VAR && src->value.var.name != NULL)
    {
        size_t len = strlen(src->value.var.name);
        node->value.var.name = (char*)calloc(len + 1, sizeof(char));
        if (!node->value.var.name)
        {
            free(node);
            *err = ERR_ALLOC;
            return NULL;
        }
        memcpy(node->value.var.name, src->value.var.name, len);
        node->value.var.name[len] = '\0';
        node->value.var.hash  = src->value.var.hash;
        node->value.var.value = src->value.var.value;
    }
    else
        node->value = src->value;

    dst->nodes_amount++;

    if (src->left)
    {
        node->left = clone_subtree(src->left, dst, depth + 1, err);
        if (*err != OK)
        {
            tree_delete_node(node, 0);
            return NULL;
        }
    }

    if (src->right)
    {
        node->right = clone_subtree(src->right, dst, depth + 1, err);
        if (*err != OK)
        {
            tree_delete_node(node, 0);
            return NULL;
        }
    }

    return node;
}

