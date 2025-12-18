#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/types.h"
#include "libs/logging/logging.h"
#include "libs/io/io.h"

#include "ast/ast.h"
#include "middleend/middleend.h"

static void print_error_context_(FILE* out, const operational_data_t* op)
{
    if (!out || !op || !op->buffer || op->buffer_size == 0) return;

    size_t off = op->error_pos;
    if (off > op->buffer_size) off = op->buffer_size;

    size_t ls = off;
    while (ls > 0 && op->buffer[ls - 1] != '\n' && op->buffer[ls - 1] != '\r')
        --ls;

    size_t le = off;
    while (le < op->buffer_size && op->buffer[le] != '\n' && op->buffer[le] != '\r')
        ++le;

    fprintf(out, "%.*s\n", (int)(le - ls), op->buffer + ls);

    for (size_t i = ls; i < off && i < le; ++i)
        fputc(op->buffer[i] == '\t' ? '\t' : ' ', out);

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

    ast_tree_t ast_tree   = (ast_tree_t){ 0 };
    int        ast_inited = 0;

    init_logging("middleend.log", DEBUG);
    log_printf(INFO, "Middle-end started");

    size_t parsed = parse_arguments(argc, argv, &in_filename, &out_filename);
    unused(parsed);

    if (!in_filename)
        FAIL_MSG("Input file not specified. Use --infile <file.east>");
    if (!out_filename)
        FAIL_MSG("Output file not specified. Use --outfile <file.east>");

    op_data.in_file = load_file(in_filename, "rb");
    if (!op_data.in_file)
        FAILF("Failed to open input file '%s'", in_filename);

    ssize_t file_size = get_file_size_stat(in_filename);
    if (file_size < 0)
        FAILF("Failed to stat input file '%s'", in_filename);

    op_data.buffer_size = (size_t)file_size;
    op_data.buffer = (char*)calloc(op_data.buffer_size + 1, 1);
    if (!op_data.buffer)
        FAILF("Failed to allocate %zu bytes for input buffer", op_data.buffer_size + 1);

    size_t bytes_read = read_file(op_data.in_file, &op_data);
    if (bytes_read == 0)
        FAILF("Failed to read input file '%s' or file is empty", in_filename);

    op_data.buffer_size = bytes_read;

    // rewind so AST reader can read from op_data.in_file
    fseek(op_data.in_file, 0, SEEK_SET);

    op_data.out_file = load_file(out_filename, "w");
    if (!op_data.out_file)
        FAILF("Failed to open output file '%s'", out_filename);

    rc = ast_tree_ctor(&ast_tree, NULL);
    if (rc != OK)
        FAIL_MSG("Failed to initialize AST tree.");
    ast_inited = 1;

    rc = ast_read_sexpr_from_op(&ast_tree, &op_data);
    if (rc != OK)
        FAIL_MSG("Failed to read .east AST.");

    int changed = 0;
    rc = ast_optimize(&ast_tree, &changed);
    if (rc != OK)
        FAIL_MSG("Optimization failed.");

    log_printf(INFO, "Optimizations finished (changed=%d)", changed);

    ast_dump_sexpr(op_data.out_file, &ast_tree, ast_tree.root);
    fprintf(op_data.out_file, "\n");

    log_printf(INFO, "Wrote optimized .east: %s", out_filename);

cleanup:
    if (ast_inited) ast_tree_dtor(&ast_tree);

    SAFE_FCLOSE(op_data.out_file);
    SAFE_FCLOSE(op_data.in_file);

    SAFE_FREE(op_data.buffer);

    close_log_file();
    return (rc == OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

