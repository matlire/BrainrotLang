#ifndef NASM_INTERNAL_H
#define NASM_INTERNAL_H

#include "backend/backend_internal.h"
#include "backend/ir/ir.h"

#define NASM_WORD_BYTES         8
#define NASM_STACK_ALIGN        16

#define NASM_INT_ARG_REG_COUNT  6
#define NASM_XMM_ARG_REG_COUNT  8

#define BE_SCREEN_STRIDE        (BE_SCREEN_WIDTH + 1)
#define BE_SCREEN_BYTES         (BE_SCREEN_STRIDE * BE_SCREEN_HEIGHT)

typedef struct
{
    int putchar_;
    int getchar_;
    int ungetchar_;

    int print_i64;
    int print_i64_no_nl;
    int print_f64;

    int ipow_i64;

    int scan_i64;
    int scan_f64;

    int floor_f64;
    int ceil_f64;
    int round_f64;
    int sqrt_f64;
    int fpow_f64;

    int clean_vm;
    int set_pixel;
    int draw;
} nasm_runtime_use_t;

extern const char* NASM_ARG_REGS[NASM_INT_ARG_REG_COUNT];
extern const char* NASM_XMM_ARG_REGS[NASM_XMM_ARG_REG_COUNT];
extern const char* NASM_PREG64[NASM_PREG_COUNT];
extern const char* NASM_XREG64[NASM_PREG_COUNT];

int    nasm_gpr_preg_is_caller_saved(int preg);
int    nasm_xmm_preg_is_caller_saved(int preg);
size_t align16 (size_t x);

long slot_disp (size_t slot);
long spill_disp(const ir_func_t* f, size_t spill);

void emit_slot (backend_t* be, size_t slot);
void emit_spill(backend_t* be, const ir_func_t* f, size_t spill);

void emit_call_save_slot(backend_t*        be,
                         const ir_func_t*  f,
                         const ir_alloc_t* a,
                         size_t            vreg);

int interval_live_across_call(const ir_alloc_t* a,
                              size_t            vreg_id,
                              size_t            ip);

void emit_load_vreg (backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch);

void emit_store_vreg(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch);

void emit_load_freg (backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch);

void emit_store_freg(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch);

void emit_mov_imm_i64_to_vreg(backend_t*        be,
                              const ir_func_t*  f,
                              const ir_alloc_t* a,
                              ir_vreg_t         v,
                              i64_t             imm);

void emit_mov_imm_f64_to_vreg(backend_t*        be,
                              const ir_func_t*  f,
                              const ir_alloc_t* a,
                              ir_vreg_t         v,
                              i64_t             bits);

void emit_load_slot_to_vreg_i64(backend_t*        be,
                                const ir_func_t*  f,
                                const ir_alloc_t* a,
                                ir_vreg_t         dst,
                                size_t            slot);

void emit_store_slot_from_vreg_i64(backend_t*        be,
                                   const ir_func_t*  f,
                                   const ir_alloc_t* a,
                                   ir_vreg_t         src,
                                   size_t            slot);

void emit_load_slot_to_vreg_f64(backend_t*        be,
                                const ir_func_t*  f,
                                const ir_alloc_t* a,
                                ir_vreg_t         dst,
                                size_t            slot);

void emit_store_slot_from_vreg_f64(backend_t*        be,
                                   const ir_func_t*  f,
                                   const ir_alloc_t* a,
                                   ir_vreg_t         src,
                                   size_t            slot);

void emit_save_live_regs_around_call(backend_t*        be,
                                     const ir_func_t*  f,
                                     const ir_alloc_t* a,
                                     size_t            ip);

void emit_restore_live_regs_around_call(backend_t*        be,
                                        const ir_func_t*  f,
                                        const ir_alloc_t* a,
                                        size_t            ip);

err_t nasm_emit_program(backend_t* be, const ast_node_t* program);
err_t nasm_emit_c_main (backend_t* be, const ast_node_t* program);

err_t nasm_emit_ir_func(backend_t* be, const ir_func_t* f, const ir_alloc_t* a);

err_t nasm_emit_cmp (backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     const ir_instr_t* in);

err_t nasm_emit_fcmp(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     const ir_instr_t* in);

err_t nasm_emit_call(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     const ir_instr_t* in,
                     size_t            ip);

err_t nasm_emit_printf_i64(backend_t*        be,
                           const ir_func_t*  f,
                           const ir_alloc_t* a,
                           const ir_instr_t* in,
                           size_t            ip);

err_t nasm_emit_printf_f64(backend_t*        be,
                           const ir_func_t*  f,
                           const ir_alloc_t* a,
                           const ir_instr_t* in,
                           size_t            ip);

err_t nasm_emit_runtime_call(backend_t*        be,
                             const ir_func_t*  f,
                             const ir_alloc_t* a,
                             const ir_instr_t* in,
                             size_t            ip);

ir_op_t binop_to_ir    (token_kind_t op);
ir_op_t binop_to_ir_f64(token_kind_t op);

const char* jcc_false_i64(token_kind_t op);
const char* jcc_false_f64(token_kind_t op);

void emit_incoming_stack_arg(backend_t* be, size_t idx);

#endif
