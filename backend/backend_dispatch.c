#include "backend.h"

#include <stdio.h>

#if defined(__BACKEND_TASM) && defined(__BACKEND_NASM)
    #error "Choose only one backend target: __BACKEND_TASM or __BACKEND_NASM"
#endif

#if !defined(__BACKEND_TASM) && !defined(__BACKEND_NASM)
    #define __BACKEND_TASM 1
#endif

err_t backend_emit_asm(const ast_tree_t* tree, operational_data_t* op_data)
{
#if defined(__BACKEND_NASM)
    return backend_emit_nasm(tree, op_data);
#elif defined(__BACKEND_TASM)
    return backend_emit_tasm(tree, op_data);
#else
    (void)tree;

    if (op_data)
    {
        op_data->error_pos = 0;
        snprintf(op_data->error_msg, sizeof(op_data->error_msg),
                 "No backend target selected at compile time");
    }
    return ERR_BAD_ARG;
#endif
}
