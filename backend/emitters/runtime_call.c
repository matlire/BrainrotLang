#include "backend/emitters/nasm_internal.h"

err_t nasm_emit_printf_i64(backend_t*             be,
                           const      ir_func_t*  f,
                           const      ir_alloc_t* a,
                           const      ir_instr_t* in,
                           size_t                 ip)
{
    emit_save_live_regs_around_call(be, f, a, ip);

    emit_load_vreg(be, f, a, in->a, "rdi");
    be_emitf(be, "    call __brl_print_i64\n");

    emit_restore_live_regs_around_call(be, f, a, ip);

    return OK;
}

err_t nasm_emit_printf_f64(backend_t*         be,
                           const  ir_func_t*  f,
                           const  ir_alloc_t* a,
                           const  ir_instr_t* in,
                           size_t             ip)
{
    emit_save_live_regs_around_call(be, f, a, ip);

    emit_load_freg(be, f, a, in->a, "xmm0");
    be_emitf(be, "    call __brl_print_f64\n");

    emit_restore_live_regs_around_call(be, f, a, ip);

    return OK;
}

err_t nasm_emit_runtime_call(backend_t*        be,
                             const ir_func_t*  f,
                             const ir_alloc_t* a,
                             const ir_instr_t* in,
                             size_t            ip)
{
#define RT(op, pre, callee, post)                                           \
        case op:                                                            \
            emit_save_live_regs_around_call(be, f, a, ip);                  \
            pre;                                                            \
            be_emitf(be, "    call " callee "\n");                          \
            emit_restore_live_regs_around_call(be, f, a, ip);               \
            post;                                                           \
            return OK

    switch (in->op)
    {
        RT(IR_OP_SCANF_I64,   , "__brl_scan_i64", emit_store_vreg(be, f, a, in->dst, "rax"));
        RT(IR_OP_SCANF_F64,   , "__brl_scan_f64", emit_store_freg(be, f, a, in->dst, "xmm0"));
        RT(IR_OP_GETCHAR_I64, , "__brl_getchar",  emit_store_vreg(be, f, a, in->dst, "rax"));

        RT(IR_OP_PUTCHAR_I64, emit_load_vreg(be, f, a, in->a, "rdi"), "__brl_putchar", );

        RT(IR_OP_RUNTIME_DRAW,  , "__brl_draw",);
        RT(IR_OP_RUNTIME_CLEAN, , "__brl_clean_vm", );

        RT(IR_OP_RUNTIME_SET_PIXEL,
           emit_load_vreg(be, f, a, in->args[0], "rdi");
           emit_load_vreg(be, f, a, in->args[1], "rsi");
           emit_load_vreg(be, f, a, in->args[2], "rdx"),
           "__brl_set_pixel",
           );

        default:
            break;
    }

#undef RT

    BE_FAIL_NODE(be, NULL, "Bad runtime op %s", ir_op_to_cstr(in->op));
}
