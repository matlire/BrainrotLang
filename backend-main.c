#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/types.h"
#include "libs/io/io.h"
#include "libs/logging/logging.h"

#include "ast/ast.h"
#include "backend/backend.h"

static char* make_asm_filename_(const char* base)
{
    if (!base) return NULL;

    const char* dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".asm") == 0)
        return strdup(base);

    size_t prefix_len = strlen(base);
    if (dot) prefix_len = (size_t)(dot - base);

    const char* ext = ".asm";
    const size_t ext_len = 4;

    char* out = (char*)calloc(prefix_len + ext_len + 1, 1);
    if (!out) return NULL;

    memcpy(out, base, prefix_len);
    memcpy(out + prefix_len, ext, ext_len);
    out[prefix_len + ext_len] = '\0';
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

    ast_tree_t ast_tree = (ast_tree_t){ 0 };
    int ast_inited = 0;

    char* asm_name = NULL;

    init_logging("backend.log", DEBUG);
    log_printf(INFO, "Backend started");

    size_t parsed = parse_arguments(argc, argv, &in_filename, &out_filename);
    unused(parsed);

    if (!CHECK(ERROR, in_filename != NULL,
               "No input file specified. Use --infile <filename>"))
    {
        FAIL_MSG("Input file not specified.");
    }

    rc = ast_tree_ctor(&ast_tree, NULL);
    if (rc != OK)
        FAIL_MSG("Failed to initialize AST tree.");
    ast_inited = 1;

    op_data.in_file = load_file(in_filename, "rb");
    if (!op_data.in_file)
        FAILF("Failed to open input AST file '%s'", in_filename);

    rc = ast_read_sexpr_from_op(&ast_tree, &op_data);
    SAFE_FCLOSE(op_data.in_file);

    if (rc != OK)
        FAIL_MSG("Failed to read/parse AST.");

    if (out_filename)
    {
        asm_name = strdup(out_filename);
        if (!asm_name) FAIL_MSG("Out of memory while copying output filename.");
    }
    else
    {
        asm_name = make_asm_filename_(in_filename);
        if (!asm_name) FAIL_MSG("Failed to build .asm output filename.");
    }

    op_data.out_file = load_file(asm_name, "w");
    if (!op_data.out_file)
        FAILF("Failed to open output file '%s' for writing", asm_name);

    rc = backend_emit_asm(&ast_tree, &op_data);
    if (rc != OK)
        FAIL_MSG("Backend codegen failed.");

    log_printf(INFO, "Backend finished successfully. Wrote: %s", asm_name);

cleanup:
    SAFE_FCLOSE(op_data.in_file);
    SAFE_FCLOSE(op_data.out_file);

    if (ast_inited)
        ast_tree_dtor(&ast_tree);

    SAFE_FREE(asm_name);

    SAFE_FREE(op_data.buffer);

    close_log_file();
    return (rc == OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

