#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libs/types.h"
#include "libs/logging/logging.h"
#include "libs/io/io.h"

#include "ast/ast.h"
#include "reverse-frontend/reverse-frontend.h"

static char* make_rot_filename_(const char* base)
{
    if (!base) return NULL;

    /* If base already ends with .rot => keep */
    const char* dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".rot") == 0)
        return strdup(base);

    /* Strip extension if present */
    size_t prefix_len = strlen(base);
    if (dot) prefix_len = (size_t)(dot - base);

    const char* ext = ".rot";
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

    fputs("^\n", out);
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

    char* rot_name = NULL;

    init_logging("reverse_frontend.log", DEBUG);
    log_printf(INFO, "Reverse-frontend started (.east -> .rot)");

    size_t parsed = parse_arguments(argc, argv, &in_filename, &out_filename);
    unused parsed;

    if (!CHECK(ERROR, in_filename != NULL,
               "No input file specified. Use --infile <filename>"))
    {
        FAIL_MSG("Input file not specified.");
    }

    /* open input .east */
    op_data.in_file = load_file(in_filename, "rb");
    if (!op_data.in_file)
        FAILF("Failed to open input file '%s'", in_filename);

    /* init AST container (nametable+symtable) */
    rc = ast_tree_ctor(&ast_tree, NULL);
    if (rc != OK)
        FAIL_MSG("Failed to initialize AST tree.");
    ast_inited = 1;

    /* read + parse S-expression AST from op_data.in_file into ast_tree
       This function also fills op_data.buffer/op_data.buffer_size for context. */
    rc = ast_read_sexpr_from_op(&ast_tree, &op_data);
    if (rc != OK)
        FAIL_MSG("Failed to read/parse .east AST.");

    SAFE_FCLOSE(op_data.in_file);

    /* output filename */
    rot_name = out_filename ? make_rot_filename_(out_filename)
                            : make_rot_filename_(in_filename);
    if (!rot_name)
        FAIL_MSG("Failed to build .rot output filename.");

    op_data.out_file = load_file(rot_name, "w");
    if (!op_data.out_file)
        FAILF("Failed to open output file '%s' for writing", rot_name);

    /* unparse */
    rc = reverse_frontend_write_rot(&op_data, &ast_tree);
    if (rc != OK)
        FAIL_MSG("Reverse-frontend failed while writing .rot");

    log_printf(INFO, "Wrote .rot: %s", rot_name);

cleanup:
    if (ast_inited) ast_tree_dtor(&ast_tree);

    SAFE_FCLOSE(op_data.in_file);
    SAFE_FCLOSE(op_data.out_file);

    SAFE_FREE(rot_name);

    /* op_data.buffer is used for error context. Depending on your ast_read_sexpr_from_op
       implementation, it may allocate it. Freeing here is correct if it was heap-allocated. */
    SAFE_FREE(op_data.buffer);
    op_data.buffer_size = 0;

    close_log_file();
    return (rc == OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}

