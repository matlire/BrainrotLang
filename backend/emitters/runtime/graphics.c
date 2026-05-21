#include "backend/emitters/runtime/runtime.h"

#ifdef __NASM_SIM_GRAPHICS

static void emit_runtime_clean_vm_(backend_t* be)
{
    be_emitf(be,
        "__brl_clean_vm:\n"
        "    lea  rdi, [__brl_screen]\n"
        "    mov  r8, %d\n"
        ".L_clean_row:\n"
        "    mov  rcx, %d\n"
        "    mov  al, ' '\n"
        "    rep  stosb\n"
        "    mov  byte [rdi], 10\n"
        "    inc  rdi\n"
        "    dec  r8\n"
        "    jne  .L_clean_row\n"
        "    xor  eax, eax\n"
        "    ret\n\n",
        BE_SCREEN_HEIGHT,
        BE_SCREEN_WIDTH);
}

static void emit_runtime_set_pixel_(backend_t* be)
{
    be_emitf(be,
        "__brl_set_pixel:\n"
        "    cmp  rdi, 0\n"
        "    jl   .L_set_pixel_ret\n"
        "    cmp  rsi, 0\n"
        "    jl   .L_set_pixel_ret\n"
        "    cmp  rdi, %d\n"
        "    jae  .L_set_pixel_ret\n"
        "    cmp  rsi, %d\n"
        "    jae  .L_set_pixel_ret\n"
        "    mov  rax, rsi\n"
        "    imul rax, %d\n"
        "    add  rax, rdi\n"
        "    lea  rcx, [__brl_screen]\n"
        "    mov  [rcx + rax], dl\n"
        ".L_set_pixel_ret:\n"
        "    xor  eax, eax\n"
        "    ret\n\n",
        BE_SCREEN_WIDTH,
        BE_SCREEN_HEIGHT,
        BE_SCREEN_STRIDE);
}

static void emit_runtime_draw_(backend_t* be)
{
    be_emitf(be,
        "__brl_draw:\n"
        "    mov  rax, 1\n"
        "    mov  rdi, 1\n"
        "    lea  rsi, [__brl_screen]\n"
        "    mov  rdx, %d\n"
        "    syscall\n"
        "    xor  eax, eax\n"
        "    ret\n\n",
        BE_SCREEN_BYTES);
}

#endif

void nasm_emit_runtime_graphics(backend_t* be, const nasm_runtime_use_t* rt)
{
#ifdef __NASM_SIM_GRAPHICS
    if (!rt)
        return;

    if (rt->clean_vm)  emit_runtime_clean_vm_(be);
    if (rt->set_pixel) emit_runtime_set_pixel_(be);
    if (rt->draw)      emit_runtime_draw_(be);
#else
    (void)be;
    (void)rt;
#endif
}
