#ifndef BACKEND_IR_H
#define BACKEND_IR_H

#include "backend/backend_internal.h"

#define NASM_PREG_COUNT 6

#define IR_MAX_CALL_ARGS 64
#define IR_NO_VREG       SIZE_MAX
#define IR_NO_LABEL      SIZE_MAX
#define IR_NO_SLOT       SIZE_MAX
#define IR_NO_FUNC       SIZE_MAX

typedef enum
{
    IR_TYPE_VOID = 0,
    IR_TYPE_I64,
    IR_TYPE_F64,
    IR_TYPE_PTR,
} ir_type_t;

typedef struct
{
    size_t    id;
    ir_type_t type;
} ir_vreg_t;

#define IR_OP_LIST(X)               \
    X(NOP)                          \
    X(LABEL)                        \
    X(JMP)                          \
    X(JZ)                           \
    X(MOV)                          \
    X(MOV_IMM_I64)                  \
    X(MOV_IMM_F64)                  \
    X(LOAD_SLOT)                    \
    X(STORE_SLOT)                   \
    X(NEG_I64)                      \
    X(NOT_I64)                      \
    X(NEG_F64)                      \
    X(I64_TO_F64)                   \
    X(F64_TO_I64)                   \
    X(ADD_I64)                      \
    X(SUB_I64)                      \
    X(MUL_I64)                      \
    X(DIV_I64)                      \
    X(POW_I64)                      \
    X(ADD_F64)                      \
    X(SUB_F64)                      \
    X(MUL_F64)                      \
    X(DIV_F64)                      \
    X(CMP_EQ_I64)                   \
    X(CMP_NE_I64)                   \
    X(CMP_LT_I64)                   \
    X(CMP_GT_I64)                   \
    X(CMP_LE_I64)                   \
    X(CMP_GE_I64)                   \
    X(CMP_EQ_F64)                   \
    X(CMP_NE_F64)                   \
    X(CMP_LT_F64)                   \
    X(CMP_GT_F64)                   \
    X(CMP_LE_F64)                   \
    X(CMP_GE_F64)                   \
    X(JCC_FALSE_I64)                \
    X(JCC_FALSE_F64)                \
    X(FLOOR_F64)                    \
    X(CEIL_F64)                     \
    X(ROUND_F64)                    \
    X(SQRT_F64)                     \
    X(POW_F64)                      \
    X(SCANF_I64)                    \
    X(SCANF_F64)                    \
    X(GETCHAR_I64)                  \
    X(CALL)                         \
    X(RET)                          \
    X(PRINTF_I64)                   \
    X(PRINTF_F64)                   \
    X(PUTCHAR_I64)                  \
    X(RUNTIME_DRAW)                 \
    X(RUNTIME_CLEAN)                \
    X(ADD_SLOT_IMM_I64)             \
    X(RET_IMM_I64)                  \
    X(RUNTIME_SET_PIXEL)

typedef enum
{
#define X(name) IR_OP_##name,
    IR_OP_LIST(X)
#undef X
} ir_op_t;

typedef struct
{
    ir_op_t op;

    ir_vreg_t dst;
    ir_vreg_t a;
    ir_vreg_t b;

    i64_t imm;

    size_t slot;
    size_t label_id;
    size_t func_id;

    ir_vreg_t args[IR_MAX_CALL_ARGS];
    size_t    arg_count;

    token_pos_t pos;
} ir_instr_t;

typedef struct
{
    size_t     name_id;
    ast_type_t type;
    size_t     slot;
} ir_slot_t;

typedef struct
{
    size_t      name_id;
    const char* asm_label;
    ast_type_t  ret_type;

    ir_instr_t* instrs;
    size_t      instr_count;
    size_t      instr_cap;

    ir_slot_t* slots;
    size_t     slot_count;
    size_t     slot_cap;

    size_t vreg_count;
    size_t label_count;

    size_t frame_slots;
    size_t spill_slots;
} ir_func_t;

typedef struct
{
    ir_func_t* funcs;
    size_t     func_count;
    size_t     func_cap;
} ir_module_t;

typedef enum
{
    IR_LOC_NONE = 0,
    IR_LOC_REG,
    IR_LOC_SPILL,
} ir_loc_kind_t;

typedef struct
{
    ir_loc_kind_t kind;
    int           preg;
    size_t        spill_slot;
} ir_alloc_loc_t;

typedef struct
{
    size_t vreg_id;
    size_t first;
    size_t last;

    ir_type_t type;

    ir_alloc_loc_t loc;
} ir_interval_t;

typedef struct
{
    ir_interval_t* intervals;

    size_t interval_count;
    size_t interval_cap;

    ir_alloc_loc_t* vreg_locs;
    size_t          vreg_loc_count;

    size_t spill_count;
} ir_alloc_t;

err_t ir_alloc_run_linear_scan(const ir_func_t* f, ir_alloc_t* out);
void  ir_alloc_dtor(ir_alloc_t* a);

err_t ir_module_ctor(ir_module_t* m);
void  ir_module_dtor(ir_module_t* m);

err_t ir_func_ctor(ir_func_t* f, size_t name_id, const char* asm_label, ast_type_t ret_type);
void  ir_func_dtor(ir_func_t* f);

ir_vreg_t ir_new_vreg (ir_func_t* f, ir_type_t type);
size_t    ir_new_label(ir_func_t* f);

err_t ir_add_slot(ir_func_t* f, size_t name_id, ast_type_t type, size_t* out_slot);

err_t ir_emit(ir_func_t* f, ir_instr_t in);

const char* ir_op_to_cstr(ir_op_t op);

int        ir_vreg_valid(ir_vreg_t v);
ir_vreg_t  ir_no_vreg   (void);

i64_t  ir_f64_to_bits  (double value);
double ir_f64_from_bits(i64_t bits);

int ir_instr_has_dst(const ir_instr_t* in);

int ir_op_is_pure(ir_op_t op);

int ir_op_is_binary_value (ir_op_t op);
int ir_op_is_unary_value  (ir_op_t op);
int ir_op_is_nullary_value(ir_op_t op);
int ir_op_is_unary_effect (ir_op_t op);

void ir_dump_func(FILE* out, const ast_tree_t* tree, const ir_func_t* f);

err_t ir_optimize_func(ir_func_t* f);

err_t ir_alloc_run_linear_scan(const ir_func_t* f, ir_alloc_t* out);
void  ir_alloc_dtor(ir_alloc_t* a);

#endif
