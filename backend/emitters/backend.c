#include "backend/emitters/nasm_internal.h"

err_t backend_emit_nasm(const ast_tree_t* tree, operational_data_t* op_data)
{
    if (!tree || !tree->root || !op_data)
        return ERR_BAD_ARG;

    op_data->error_pos    = 0;
    op_data->error_msg[0] = '\0';

    backend_t be = { 0 };
    be.tree      = tree;
    be.op        = op_data;

    const ast_node_t* program = tree->root;

    if (program->kind != ASTK_PROGRAM)
    {
        be_set_error(&be, program->pos, program->pos.offset,
                     "Root is not PROGRAM");
        return ERR_SYNTAX;
    }

    err_t rc = be_collect_funcs(&be, program, "__fn_%s");
    if (rc != OK)
        goto cleanup;

    size_t main_id = be_find_name(tree, "main");
    if (main_id == SIZE_MAX || !be_find_func(&be, main_id))
    {
        be_set_error(&be, (token_pos_t){1, 1, 0}, 0,
                     "No function 'main' found");
        rc = ERR_SYNTAX;
        goto cleanup;
    }

    rc = nasm_emit_program(&be, program);

cleanup:
    be_free(&be);
    return rc;
}


