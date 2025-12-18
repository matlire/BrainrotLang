#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/types.h"
#include "libs/logging/logging.h"
#include "libs/io/io.h"

#include "lexer/lexer.h"
#include "ast/ast.h"
#include "ast/syntax_analyzer.h"
#include "ast/dump/dump.h"

static char* make_east_filename_(const char* base)
{
    if (!base) return NULL;

    const char* dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".east") == 0)
        return strdup(base);

    size_t prefix_len = strlen(base);
    if (dot) prefix_len = (size_t)(dot - base);

    const char* ext = ".east";
    const size_t ext_len = 5;

    char* out = (char*)calloc(prefix_len + ext_len + 1, 1);
    if (!out) return NULL;

    out = strdup(base);
    strcat(out, ext);
    return out;
}

static void print_error_context_(FILE* out, const operational_data_t* op)
{
    if (!out || !op || !op->buffer || op->buffer_size == 0) return;

    size_t off = op->error_pos;
    if (off > op->buffer_size) 
        off = op->buffer_size;

    size_t ls = off;
    while (ls > 0 && op->buffer[ls - 1] != '\n' && op->buffer[ls - 1] != '\r')
        --ls;

    size_t le = off;
    while (le < op->buffer_size && op->buffer[le] != '\n' && op->buffer[le] != '\r')
        ++le;

    fprintf(out, "%.*s\n", (int)(le - ls), op->buffer + ls);

    for (size_t i = ls; i < off && i < le; ++i)
        fprintf(out, "%c", op->buffer[i] == '\t' ? '\t' : ' ');

    fprintf(out, "^\n");
}

#define SAFE_FCLOSE(fp)                                            \
    block_begin                                                    \
        if ((fp) && (fp) != stdout) { fclose((fp)); (fp) = NULL; } \
    block_end

#define SAFE_FREE(p)                    \
    block_begin                         \
        if (p) { free(p); (p) = NULL; } \
    block_end

#define FAIL_MSG(msg)                                                            \
    block_begin                                                                  \
        if (op_data.error_msg[0] == '\0')                                        \
            snprintf(op_data.error_msg, sizeof(op_data.error_msg), "%s", (msg)); \
        fprintf(stderr, "%s\n", op_data.error_msg);                              \
        print_error_context_(stderr, &op_data);                                  \
        log_printf(ERROR, "%s", op_data.error_msg);                              \
        rc = ERR_SYNTAX;                                                         \
        goto cleanup;                                                            \
    block_end

#define FAILF(fmt, ...)                                                             \
    block_begin                                                                     \
        snprintf(op_data.error_msg, sizeof(op_data.error_msg), (fmt), __VA_ARGS__); \
        fprintf(stderr, "%s\n", op_data.error_msg);                                 \
        print_error_context_(stderr, &op_data);                                     \
        log_printf(ERROR, "%s", op_data.error_msg);                                 \
        rc = ERR_SYNTAX;                                                            \
        goto cleanup;                                                               \
    block_end

int main(int argc, char* const argv[])
{
    err_t rc = OK;

    const char* in_filename  = NULL;
    const char* out_filename = NULL;

    operational_data_t op_data = (operational_data_t){ 0 };

    token_t*    tokens           = NULL;
    size_t      token_count      = 0;
    nametable_t nametable        = (nametable_t){ 0 };
    int         nametable_inited = 0;
    int         nametable_moved  = 0;

    ast_tree_t        ast_tree   = (ast_tree_t){ 0 };
    int               ast_inited = 0;
    syntax_analyzer_t sa         = (syntax_analyzer_t){ 0 };
    int               sa_inited  = 0;

    char* east_name = NULL;
    FILE* east      = NULL;

    init_logging("frontend.log", DEBUG);
    log_printf(INFO, "Frontend started");

    size_t parsed = parse_arguments(argc, argv, &in_filename, &out_filename);
    unused parsed;

    if (!CHECK(ERROR, in_filename != NULL,
               "No input file specified. Use --infile <filename>"))
    {
        FAIL_MSG("Input file not specified.");
    }

    // load input
    op_data.in_file = load_file(in_filename, "rb");
    if (!op_data.in_file)
        FAILF("Failed to open input file '%s'", in_filename);

    ssize_t file_size = get_file_size_stat(in_filename);
    if (!CHECK(ERROR, file_size >= 0, "Failed to stat input file '%s'", in_filename))
        FAILF("Failed to stat input file '%s'", in_filename);

    op_data.buffer_size = (size_t)file_size;
    op_data.buffer = (char*)calloc(op_data.buffer_size + 1, 1);
    if (!op_data.buffer)
        FAILF("Failed to allocate %zu bytes for input buffer", op_data.buffer_size + 1);

    size_t bytes_read = read_file(op_data.in_file, &op_data);
    SAFE_FCLOSE(op_data.in_file);

    if (!CHECK(ERROR, bytes_read > 0,
               "Failed to read input file '%s' or file is empty",
               in_filename))
        FAILF("Failed to read input file '%s' or file is empty", in_filename);

    op_data.buffer_size = bytes_read;

    // lexer
    rc = lexer_stream(&op_data, &tokens, &token_count, &nametable);
    if (rc != OK)
        FAIL_MSG("Lexing failed.");
    nametable_inited = 1;

    log_printf(INFO, "Lexing finished successfully, %zu tokens", token_count);

    // AST tree ctor
    rc = ast_tree_ctor(&ast_tree, &nametable);
    if (rc != OK)
        FAIL_MSG("Failed to initialize AST tree.");
    ast_inited = 1;
    nametable_moved = 1;

    // parse
    rc = syntax_analyzer_ctor(&sa, &op_data, tokens, token_count, &ast_tree);
    if (rc != OK)
        FAIL_MSG("Failed to initialize syntax analyzer.");
    sa_inited = 1;

    rc = syntax_analyze(&sa);
    if (rc != OK)
        FAIL_MSG("Parsing failed.");

    FILE* dump_file = load_file("frontend-ast-tree-dump.html", "w");
    if (!dump_file)
        FAILF("Failed to open dump file '%s' for writing", "frontend-ast-tree-dump.html");
    ast_dump_graphviz_html(&ast_tree, dump_file);

    log_printf(INFO, "Parsing finished successfully");

    // save .east
    east_name = out_filename ? make_east_filename_(out_filename)
                             : make_east_filename_(in_filename);

    if (!east_name)
        FAIL_MSG("Failed to build .east output filename.");

    east = load_file(east_name, "w");
    if (!east)
        FAILF("Failed to open output file '%s' for writing", east_name);

    ast_dump_sexpr(east, &ast_tree, ast_tree.root);
    fprintf(east, "\n");

    log_printf(INFO, "Wrote AST dump: %s", east_name);

cleanup:
    if (sa_inited)  syntax_analyzer_dtor(&sa);
    if (ast_inited) ast_tree_dtor(&ast_tree);

    SAFE_FCLOSE(east);
    SAFE_FREE(east_name);

    SAFE_FREE(tokens);

    if (nametable_inited && !nametable_moved)
        nametable_dtor(&nametable);

    SAFE_FCLOSE(op_data.in_file);
    if (op_data.out_file && op_data.out_file != stdout)
        SAFE_FCLOSE(op_data.out_file);

    SAFE_FREE(op_data.buffer);

    close_log_file();
    return (rc == OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
