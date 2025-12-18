#include "dump.h"

#include <stdlib.h>
#include <string.h>

static size_t s_ast_img_counter = 0;

static const char* ast_builtin_unary_to_cstr_(ast_builtin_unary_t id)
{
    switch (id)
    {
        case AST_BUILTIN_FLOOR: return "stan";
        case AST_BUILTIN_CEIL:  return "aura";
        case AST_BUILTIN_ROUND: return "delulu";
        case AST_BUILTIN_ITOF:  return "goober";
        case AST_BUILTIN_FTOI:  return "bozo";
        default:                return "?";
    }
}

static void html_escape_(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t w = 0;
    for (size_t i = 0; src[i] && w + 1 < cap; ++i)
    {
        const char c = src[i];
        const char* rep = NULL;

        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (c == '"') rep = "&quot;";

        if (rep)
        {
            const size_t n = strlen(rep);
            if (w + n >= cap) break;
            memcpy(dst + w, rep, n);
            w += n;
        }
        else
        {
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
}

#define AST_GV_PAYLOAD_LIST(X)                                                   \
    X(ASTK_FUNC, {                                                               \
        snprintf(tmp, sizeof(tmp), "name=%s, ret=%s",                            \
            ast_name_cstr(t, n->u.func.name_id),                                 \
            ast_type_to_cstr(n->u.func.ret_type));                               \
    })                                                                           \
    X(ASTK_PARAM, {                                                              \
        snprintf(tmp, sizeof(tmp), "name=%s, type=%s",                           \
            ast_name_cstr(t, n->u.param.name_id),                                \
            ast_type_to_cstr(n->u.param.type));                                  \
    })                                                                           \
    X(ASTK_VAR_DECL, {                                                           \
        snprintf(tmp, sizeof(tmp), "name=%s, type=%s",                           \
            ast_name_cstr(t, n->u.vdecl.name_id),                                \
            ast_type_to_cstr(n->u.vdecl.type));                                  \
    })                                                                           \
    X(ASTK_ASSIGN, {                                                             \
        snprintf(tmp, sizeof(tmp), "name=%s",                                    \
            ast_name_cstr(t, n->u.assign.name_id));                              \
    })                                                                           \
    X(ASTK_IDENT, {                                                              \
        snprintf(tmp, sizeof(tmp), "name=%s",                                    \
            ast_name_cstr(t, n->u.ident.name_id));                               \
    })                                                                           \
    X(ASTK_CALL, {                                                               \
        snprintf(tmp, sizeof(tmp), "name=%s",                                    \
            ast_name_cstr(t, n->u.call.name_id));                                \
    })                                                                           \
    X(ASTK_NUM_LIT, {                                                            \
        if (n->u.num.lit_type == LIT_INT)                                        \
            snprintf(tmp, sizeof(tmp), "int=%lld", (long long)n->u.num.lit.i64); \
        else if (n->u.num.lit_type == LIT_FLOAT)                                 \
            snprintf(tmp, sizeof(tmp), "float=%g", (double)n->u.num.lit.f64);    \
        else                                                                     \
            snprintf(tmp, sizeof(tmp), "lit=?");                                 \
    })                                                                           \
    X(ASTK_STR_LIT, {                                                            \
        snprintf(tmp, sizeof(tmp), "str_len=%zu", n->u.str.len);                 \
    })                                                                           \
    X(ASTK_UNARY, {                                                              \
        snprintf(tmp, sizeof(tmp), "op=%s", token_kind_to_cstr(n->u.unary.op));  \
    })                                                                           \
    X(ASTK_BINARY, {                                                             \
        snprintf(tmp, sizeof(tmp), "op=%s", token_kind_to_cstr(n->u.binary.op)); \
    })                                                                           \
    X(ASTK_BUILTIN_UNARY, {                                                      \
        snprintf(tmp, sizeof(tmp), "builtin=%s",                                 \
            ast_builtin_unary_to_cstr_(n->u.builtin_unary.id));                  \
    })

static void node_payload_(char* out, size_t cap, const ast_tree_t* t, const ast_node_t* n)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!t || !n) return;

    char tmp[512] = {0};

    switch (n->kind)
    {
#define AST_GV_CASE(kind, code) case kind: code break;
        AST_GV_PAYLOAD_LIST(AST_GV_CASE)
#undef AST_GV_CASE
        default:
            tmp[0] = '\0';
            break;
    }

    html_escape_(out, cap, tmp);
}

typedef struct
{
    const ast_node_t* node;
    size_t           id;
} NodeInfo;

static size_t find_index_by_ptr_(const NodeInfo* arr, size_t n, const ast_node_t* p)
{
    for (size_t i = 0; i < n; ++i)
        if (arr[i].node == p) return i;
    return (size_t)-1;
}

void ast_dump_graphviz_html(const ast_tree_t* tree, FILE* out_html)
{
    if (!out_html) return;

    system("mkdir -p temp");

    const char* dot_path = "temp/ast_graph.dot";

    char svg_name[64] = {0};
    snprintf(svg_name, sizeof(svg_name), "ast%zu.svg", s_ast_img_counter++);

    char svg_path[512] = {0};
    snprintf(svg_path, sizeof(svg_path), "temp/t%s", svg_name);

    FILE* dot = fopen(dot_path, "w");
    if (!dot)
    {
        fprintf(out_html, "<p><b>AST:</b> failed to open %s</p>\n<hr/>\n", dot_path);
        return;
    }

    const char* EDGE_CHILD   = "#98A2B3";
    const char* EDGE_SIBLING = "#98A2B3";
    const char* OUT_ROOT     = "#16A34A";
    const char* OUT_NODE     = "#475467";
    const char* FILL_NODE    = "#F9FAFB";
    const char* FILL_ROOT    = "#E6F4EA";
    const char* CELL_BG      = "#FFFFFF";
    const char* TABLE_BRD    = "#D0D5DD";
    const char* TXT_COLOR    = "#111827";

    fprintf(dot, "digraph AST {\n");
    fprintf(dot, "rankdir=TB;\n");
    fprintf(dot, "bgcolor=\"white\";\n");
    fprintf(dot, "fontname=\"monospace\";\n");
    fprintf(dot, "fontsize=18;\n");

    fprintf(dot,
        "node [shape=box, style=\"rounded,filled\", color=\"%s\", fillcolor=\"%s\", "
        "fontname=\"monospace\", fontsize=10];\n",
        OUT_NODE, FILL_NODE);

    fprintf(dot,
        "edge [color=\"#98A2B3\", penwidth=1.7, arrowsize=0.8, arrowhead=vee, "
        "fontname=\"monospace\", fontsize=9];\n");

    if (!tree || !tree->root)
    {
        fprintf(dot,
            "empty [label=\"<empty AST>\", color=\"#9CA3AF\", "
            "fontcolor=\"#9CA3AF\", fillcolor=\"#F3F4F6\"];\n");
        fprintf(dot, "}\n");
        fclose(dot);

        char cmd_empty[4096];
        snprintf(cmd_empty, sizeof(cmd_empty), "dot -T svg \"%s\" -o \"%s\"", dot_path, svg_path);
        system(cmd_empty);

        fprintf(out_html, "<h2>AST</h2>\n");
        fprintf(out_html, "<h3>Nodes: 0</h3>\n");
        fprintf(out_html, "<img src=\"temp/t%s\" />\n", svg_name);
        fprintf(out_html, "<hr/>\n");
        return;
    }

    size_t cap_nodes = 64;
    size_t cap_q     = 64;
    size_t n         = 0;
    size_t head      = 0;
    size_t tail      = 0;

    NodeInfo* nodes = (NodeInfo*)calloc(cap_nodes, sizeof(NodeInfo));
    const ast_node_t** q = (const ast_node_t**)calloc(cap_q, sizeof(const ast_node_t*));
    if (!nodes || !q)
    {
        free(nodes);
        free(q);
        fclose(dot);
        fprintf(out_html, "<p><b>AST:</b> alloc failed</p>\n<hr/>\n");
        return;
    }

    q[tail++] = tree->root;

    while (head < tail)
    {
        const ast_node_t* cur = q[head++];

        if (n == cap_nodes)
        {
            size_t nc = cap_nodes * 2;
            NodeInfo* nn = (NodeInfo*)realloc(nodes, nc * sizeof(NodeInfo));
            if (!nn)
            {
                free(nodes);
                free(q);
                fclose(dot);
                fprintf(out_html, "<p><b>AST:</b> realloc failed</p>\n<hr/>\n");
                return;
            }
            nodes = nn;
            cap_nodes = nc;
        }

        nodes[n].node = cur;
        nodes[n].id = n;
        n++;

        if (cur->left)
        {
            if (tail == cap_q)
            {
                size_t nq = cap_q * 2;
                const ast_node_t** qq = (const ast_node_t**)realloc(q, nq * sizeof(*q));
                if (!qq)
                {
                    free(nodes);
                    free(q);
                    fclose(dot);
                    fprintf(out_html, "<p><b>AST:</b> realloc failed</p>\n<hr/>\n");
                    return;
                }
                q = qq;
                cap_q = nq;
            }
            q[tail++] = cur->left;
        }

        if (cur->right)
        {
            if (tail == cap_q)
            {
                size_t nq = cap_q * 2;
                const ast_node_t** qq = (const ast_node_t**)realloc(q, nq * sizeof(*q));
                if (!qq)
                {
                    free(nodes);
                    free(q);
                    fclose(dot);
                    fprintf(out_html, "<p><b>AST:</b> realloc failed</p>\n<hr/>\n");
                    return;
                }
                q = qq;
                cap_q = nq;
            }
            q[tail++] = cur->right;
        }
    }

    free(q);

    for (size_t i = 0; i < n; ++i)
    {
        const ast_node_t* p = nodes[i].node;
        const int is_root = (p == tree->root);

        const char* outline = is_root ? OUT_ROOT : OUT_NODE;
        const char* fill    = is_root ? FILL_ROOT : FILL_NODE;

        char payload[1024] = {0};
        node_payload_(payload, sizeof(payload), tree, p);

        fprintf(dot,
            "n%zu [shape=plain, color=\"%s\", fillcolor=\"%s\", penwidth=2.0, label=<"
            "<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" COLOR=\"%s\">"
            "<TR><TD COLSPAN=\"2\" BGCOLOR=\"%s\"><B><FONT COLOR=\"%s\">%s</FONT></B></TD></TR>"
            "<TR><TD ALIGN=\"LEFT\">addr</TD><TD ALIGN=\"LEFT\">%p</TD></TR>"
            "<TR><TD ALIGN=\"LEFT\">kind</TD><TD ALIGN=\"LEFT\">%s</TD></TR>"
            "<TR><TD ALIGN=\"LEFT\">type</TD><TD ALIGN=\"LEFT\">%s</TD></TR>"
            "<TR><TD ALIGN=\"LEFT\">pos</TD><TD ALIGN=\"LEFT\">%zu:%zu</TD></TR>"
            "<TR><TD ALIGN=\"LEFT\">payload</TD><TD ALIGN=\"LEFT\">%s</TD></TR>"
            "<TR><TD PORT=\"L\" ALIGN=\"LEFT\">child: %p</TD>"
            "<TD PORT=\"R\" ALIGN=\"LEFT\">sib: %p</TD></TR>"
            "</TABLE>"
            ">];\n",
            nodes[i].id,
            outline, fill,
            TABLE_BRD, CELL_BG, TXT_COLOR,
            is_root ? "ROOT" : "NODE",
            (void*)p,
            ast_kind_to_cstr(p->kind),
            ast_type_to_cstr(p->type),
            p->pos.line, p->pos.column,
            payload[0] ? payload : "&nbsp;",
            (void*)p->left, (void*)p->right
        );
    }

    for (size_t i = 0; i < n; ++i)
    {
        const ast_node_t* p = nodes[i].node;

        if (p->left)
        {
            size_t j = find_index_by_ptr_(nodes, n, p->left);
            if (j != (size_t)-1)
                fprintf(dot, "n%zu:L -> n%zu [color=\"%s\", penwidth=1.9];\n",
                        nodes[i].id, nodes[j].id, EDGE_CHILD);
        }

        if (p->right)
        {
            size_t j = find_index_by_ptr_(nodes, n, p->right);
            if (j != (size_t)-1)
                fprintf(dot, "n%zu:R -> n%zu [color=\"%s\", penwidth=1.9, style=dashed];\n",
                        nodes[i].id, nodes[j].id, EDGE_SIBLING);
        }
    }

    fprintf(dot, "}\n");
    fclose(dot);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "dot -T svg \"%s\" -o \"%s\"", dot_path, svg_path);
    const int rc = system(cmd);

    fprintf(out_html, "<h2>AST</h2>\n");
    fprintf(out_html, "<h3>Nodes: %zu</h3>\n", n);
    fprintf(out_html, "<h3>Root: 0x%p</h3>\n", (void*)tree->root);
    fprintf(out_html, "<h3>dot rc: %d</h3>\n", rc);
    fprintf(out_html, "<img src=\"temp/t%s\" />\n", svg_name);
    fprintf(out_html, "<hr/>\n");

    free(nodes);
}
