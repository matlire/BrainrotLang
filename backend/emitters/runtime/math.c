#include "backend/emitters/runtime/runtime.h"

static void emit_runtime_ipow_i64_(backend_t* be)
{
    be_emitf(be,
        "__brl_ipow_i64:\n"
        "    mov  rax, 1\n"
        "    test rsi, rsi\n"
        "    js   .L_ipow_done\n"
        ".L_ipow_loop:\n"
        "    test rsi, rsi\n"
        "    jz   .L_ipow_done\n"
        "    test rsi, 1\n"
        "    jz   .L_ipow_skip_mul\n"
        "    imul rax, rdi\n"
        ".L_ipow_skip_mul:\n"
        "    imul rdi, rdi\n"
        "    sar  rsi, 1\n"
        "    jmp  .L_ipow_loop\n"
        ".L_ipow_done:\n"
        "    ret\n\n");
}

static void emit_runtime_floor_f64_(backend_t* be)
{
    be_emitf(be,
        "__brl_floor_f64:\n"
        "    cvttsd2si rax, xmm0\n"
        "    cvtsi2sd xmm1, rax\n"
        "    ucomisd xmm1, xmm0\n"
        "    jbe  .L_floor_ret\n"
        "    sub  rax, 1\n"
        ".L_floor_ret:\n"
        "    cvtsi2sd xmm0, rax\n"
        "    ret\n\n");
}

static void emit_runtime_ceil_f64_(backend_t* be)
{
    be_emitf(be,
        "__brl_ceil_f64:\n"
        "    cvttsd2si rax, xmm0\n"
        "    cvtsi2sd xmm1, rax\n"
        "    ucomisd xmm1, xmm0\n"
        "    jae  .L_ceil_ret\n"
        "    add  rax, 1\n"
        ".L_ceil_ret:\n"
        "    cvtsi2sd xmm0, rax\n"
        "    ret\n\n");
}

static void emit_runtime_round_f64_(backend_t* be)
{
    be_emitf(be,
        "__brl_round_f64:\n"
        "    addsd xmm0, [__f64_half]\n"
        "    jmp  __brl_floor_f64\n\n");
}

static void emit_runtime_sqrt_f64_(backend_t* be)
{
    be_emitf(be,
        "__brl_sqrt_f64:\n"
        "    ; Newton sqrt, input/output xmm0\n"
        "    ucomisd xmm0, [__f64_zero]\n"
        "    jbe  .L_sqrt_ret\n"
        "    movapd xmm1, xmm0\n"
        "    mov  rcx, 24\n"
        ".L_sqrt_loop:\n"
        "    movapd xmm2, xmm0\n"
        "    divsd xmm2, xmm1\n"
        "    addsd xmm1, xmm2\n"
        "    mulsd xmm1, [__f64_half]\n"
        "    dec  rcx\n"
        "    jne  .L_sqrt_loop\n"
        "    movapd xmm0, xmm1\n"
        ".L_sqrt_ret:\n"
        "    ret\n\n");
}

static void emit_runtime_fpow_f64_(backend_t* be)
{
    be_emitf(be,
        "__brl_fpow_f64:\n"
        "    ; input: xmm0=base, xmm1=exponent. Supports integer exponent only.\n"
        "    cvttsd2si rdi, xmm1\n"
        "    movapd xmm2, xmm0\n"
        "    movsd xmm0, [__f64_one]\n"
        "    cmp  rdi, 0\n"
        "    jge  .L_fpow_loop\n"
        "    neg  rdi\n"
        "    mov  r8d, 1\n"
        "    jmp  .L_fpow_loop_start\n"
        ".L_fpow_loop:\n"
        "    xor  r8d, r8d\n"
        ".L_fpow_loop_start:\n"
        "    test rdi, rdi\n"
        "    jz   .L_fpow_done\n"
        "    test rdi, 1\n"
        "    jz   .L_fpow_skip\n"
        "    mulsd xmm0, xmm2\n"
        ".L_fpow_skip:\n"
        "    mulsd xmm2, xmm2\n"
        "    sar  rdi, 1\n"
        "    jmp  .L_fpow_loop_start\n"
        ".L_fpow_done:\n"
        "    test r8d, r8d\n"
        "    jz   .L_fpow_ret\n"
        "    movsd xmm1, [__f64_one]\n"
        "    divsd xmm1, xmm0\n"
        "    movapd xmm0, xmm1\n"
        ".L_fpow_ret:\n"
        "    ret\n\n");
}

void nasm_emit_runtime_math(backend_t* be, const nasm_runtime_use_t* rt)
{
    if (!rt)
        return;

    if (rt->ipow_i64)  emit_runtime_ipow_i64_(be);
    if (rt->floor_f64) emit_runtime_floor_f64_(be);
    if (rt->ceil_f64)  emit_runtime_ceil_f64_(be);
    if (rt->round_f64) emit_runtime_round_f64_(be);
    if (rt->sqrt_f64)  emit_runtime_sqrt_f64_(be);
    if (rt->fpow_f64)  emit_runtime_fpow_f64_(be);
}
