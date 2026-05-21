#include "backend/emitters/nasm_internal.h"

static void compute_stack_arg_flags_(const func_meta_t* callee,
                                     unsigned char*     stack_flags,
                                     size_t*            out_stack_count)
{
    size_t regs[2]    = { 0 };
    size_t limits[2]  = { NASM_INT_ARG_REG_COUNT, NASM_XMM_ARG_REG_COUNT };
    size_t stack_count = 0;

    for (size_t i = 0; i < callee->param_count; ++i)
    {
        int f64 = callee->param_types[i] == AST_TYPE_FLOAT;

        stack_flags[i] = (unsigned char)(regs[f64]++ >= limits[f64]);
        stack_count += stack_flags[i];
    }

    if (out_stack_count)
        *out_stack_count = stack_count;
}

err_t nasm_emit_call(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     const ir_instr_t* in,
                     size_t            ip)
{
    const func_meta_t* callee = be_find_func(be, in->func_id);

    BE_CHECK(be, callee != NULL, NULL, "Unknown function in IR CALL");
    BE_CHECK(be, in->arg_count == callee->param_count,
             NULL, "IR CALL arg count mismatch");
    BE_CHECK(be, in->arg_count <= IR_MAX_CALL_ARGS,
             NULL, "IR supports max %d call args", IR_MAX_CALL_ARGS);

    unsigned char stack_flags[IR_MAX_CALL_ARGS] = { 0 };
    size_t stack_count = 0;

    compute_stack_arg_flags_(callee, stack_flags, &stack_count);

#define EMIT_STACK_ARG(arg)                                             \
    do                                                                  \
    {                                                                   \
        if ((arg).type == IR_TYPE_F64)                                  \
        {                                                               \
            emit_load_freg(be, f, a, arg, "xmm0");                      \
            be_emitf(be, "    sub  rsp, 8\n");                          \
            be_emitf(be, "    movsd [rsp], xmm0\n");                    \
        }                                                               \
        else                                                            \
        {                                                               \
            emit_load_vreg(be, f, a, arg, "rax");                       \
            be_emitf(be, "    push rax\n");                             \
        }                                                               \
    } while (0)

#define EMIT_REG_ARG(i)                                                 \
    do                                                                  \
    {                                                                   \
        if (callee->param_types[i] == AST_TYPE_FLOAT)                   \
        {                                                               \
            BE_CHECK(be, farg < NASM_XMM_ARG_REG_COUNT,                 \
                     NULL, "Internal: bad float arg classification");   \
            emit_load_freg(be, f, a, in->args[i],                       \
                           NASM_XMM_ARG_REGS[farg++]);                  \
        }                                                               \
        else                                                            \
        {                                                               \
            BE_CHECK(be, iarg < NASM_INT_ARG_REG_COUNT,                 \
                     NULL, "Internal: bad int arg classification");     \
            emit_load_vreg(be, f, a, in->args[i],                       \
                           NASM_ARG_REGS[iarg++]);                      \
        }                                                               \
    } while (0)

#define EMIT_CALL_RET()                                                 \
    do                                                                  \
    {                                                                   \
        if (in->dst.id != IR_NO_VREG)                                   \
        {                                                               \
            if (in->dst.type == IR_TYPE_F64)                            \
                emit_store_freg(be, f, a, in->dst, "xmm0");             \
            else                                                        \
                emit_store_vreg(be, f, a, in->dst, "rax");              \
        }                                                               \
    } while (0)

    emit_save_live_regs_around_call(be, f, a, ip);

    size_t stack_pad = (stack_count % 2) ? NASM_WORD_BYTES : 0;

    if (stack_pad)
        be_emitf(be, "    sub  rsp, 8 ; call stack alignment pad\n");

    for (size_t i = in->arg_count; i-- > 0; )
        if (stack_flags[i])
            EMIT_STACK_ARG(in->args[i]);

    size_t iarg = 0;
    size_t farg = 0;

    for (size_t i = 0; i < in->arg_count; ++i)
        if (!stack_flags[i])
            EMIT_REG_ARG(i);

    be_emitf(be, "    call %s\n", callee->label);

    if (stack_count || stack_pad)
        be_emitf(be, "    add  rsp, %zu\n",
                 stack_count * NASM_WORD_BYTES + stack_pad);

    emit_restore_live_regs_around_call(be, f, a, ip);

    EMIT_CALL_RET();

#undef EMIT_STACK_ARG
#undef EMIT_REG_ARG
#undef EMIT_CALL_RET

    return OK;
}
