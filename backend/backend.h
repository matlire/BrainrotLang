#ifndef BACKEND_H
#define BACKEND_H

#include "../ast/ast.h"
#include "../libs/io/io.h"

typedef enum
{
    REG_RET_I = 0,   // x0  - integer/pointer return
    REG_SP    = 14,  // x14 - RAM stack pointer
    REG_BP    = 15,  // x15 - RAM base pointer
    REG_TMPA  = 13,  // x13 - temp address register for PUSHM/POPM
    REG_RET_F = 0,   // fx0 - float return
    REG_TMP_F = 1,   // fx1 scratch temp
} reserved_regs_t;

typedef struct 
{
    size_t     name_id;
    char*      label;        // ":fn_<name>"
    ast_type_t ret_type; 

    size_t      param_count;
    ast_type_t* param_types; // param_count items
    size_t      local_count; // number of VAR_DECL inside body
} func_meta_t;

typedef struct binding_t
{
    size_t     name_id;
    ast_type_t type;
    size_t     offset;   // offset relative to BP (0 = saved BP, 1..params..locals)
    size_t     depth;    // scope depth where introduced
} binding_t;

typedef struct 
{
    char* end_label;
} loop_ctx_t;

typedef struct
{
    const ast_tree_t*   tree;
    operational_data_t* op;

    func_meta_t* funcs;
    size_t       func_amount;
    size_t       func_cap;

    binding_t* binds;
    size_t     bind_amount;
    size_t     bind_cap;

    loop_ctx_t* loops;
    size_t      loop_amount;
    size_t      loop_cap;

    size_t scope_depth;
    size_t label_counter;

    const func_meta_t* cur_fn;
    size_t             next_local_offset; 
    char*              fn_end_label;
} backend_t;

#define BE_SCREEN_WIDTH 128

err_t backend_emit_asm(const ast_tree_t* tree, operational_data_t* op_data);

#endif
