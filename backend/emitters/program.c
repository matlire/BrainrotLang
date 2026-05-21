#include "backend/emitters/nasm_internal.h"
#include "backend/emitters/lower/lower.h"
#include "backend/emitters/runtime/runtime.h"
#include "backend/ir/ir.h"

#if defined(__DUMP_IR)

static void nasm_dump_ir_stage_(FILE*             dump,
                                const ast_tree_t* tree,
                                const ir_func_t*  ir,
                                const char*       stage)
{
    if (!dump || !tree || !ir || !stage)
        return;

    fprintf(dump, "\n");
    fprintf(dump, "============================================================\n");
    fprintf(dump, "IR DUMP: %s\n", stage);
    fprintf(dump, "============================================================\n");

    ir_dump_func(dump, tree, ir);

    fprintf(dump, "\n");
    fflush(dump);
}

#endif

const char* NASM_ARG_REGS[NASM_INT_ARG_REG_COUNT] =
{
    "rdi", "rsi", "rdx", "rcx", "r8", "r9"
};

const char* NASM_XMM_ARG_REGS[NASM_XMM_ARG_REG_COUNT] =
{
    "xmm0", "xmm1", "xmm2", "xmm3",
    "xmm4", "xmm5", "xmm6", "xmm7"
};

const char* NASM_PREG64[NASM_PREG_COUNT] =
{
    "r10", "r11", "r12", "r13", "r14", "r15"
};

const char* NASM_XREG64[NASM_PREG_COUNT] =
{
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13"
};


static void nasm_emit_header_(backend_t* be);
static void nasm_emit_sections_(backend_t* be);


inline int nasm_gpr_preg_is_caller_saved(int preg)
{
    return preg == 0 || preg == 1; // r10, r11
}

inline int nasm_xmm_preg_is_caller_saved(int preg)
{
    (void)preg;
    return 1;
}

inline size_t align16(size_t x)
{
    return (x + 15u) & ~15u;
}

err_t nasm_emit_c_main(backend_t* be, const ast_node_t* program)
{
    size_t main_id = be_find_name(be->tree, "main");
    const func_meta_t* main_fn = be_find_func(be, main_id);

    BE_CHECK(be, main_fn != NULL, program, "No main() metadata");

    be_emitf(be, "_start:\n");
    be_emitf(be, "    and  rsp, -16\n");

#ifdef __NASM_SIM_GRAPHICS
    be_emitf(be, "    call __brl_clean_vm\n");
#endif

    be_emitf(be, "    call %s\n", main_fn->label);
    be_emitf(be, "    mov  rdi, rax\n");
    be_emitf(be, "    mov  rax, 60\n");
    be_emitf(be, "    syscall\n\n");

    return OK;
}

err_t nasm_emit_program(backend_t* be, const ast_node_t* program)
{
    nasm_runtime_use_t rt = { 0 };
    err_t rc = OK;
    FILE* ir_dump = NULL;

#if defined(__DUMP_IR)
    ir_dump = load_file("backend-ir-dump.txt", "w");
    if (!ir_dump)
    {
        token_pos_t pos = program ? program->pos
                                  : (token_pos_t){ .line = 1, .column = 1, .offset = 0 };

        be_set_error(be,
                     pos,
                     pos.offset,
                     "Failed to open backend-ir-dump.txt");

        return ERR_SYNTAX;
    }

    fprintf(ir_dump, "BrainrotLang backend IR dump\n");
    fprintf(ir_dump, "Target: NASM\n");
    fprintf(ir_dump, "\n");
#endif
    nasm_emit_header_(be);
    nasm_emit_sections_(be);

    rc = nasm_emit_c_main(be, program);

#define RC_CH(rc)           \
    if (rc != OK)           \
        goto cleanup_func;  \

    RC_CH(rc)
    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        ir_func_t ir     = { 0 };
        ir_alloc_t alloc = { 0 };

        rc = nasm_lower_func(be, fn, &ir);
        RC_CH(rc)

#if defined(__DUMP_IR)
        {
            const char* fn_name = ast_name_cstr(be->tree, ir.name_id);
            char stage[256] = { 0 };
            snprintf(stage, sizeof(stage), "after lowering: %s",
                     fn_name ? fn_name : "<unknown>");
            nasm_dump_ir_stage_(ir_dump, be->tree, &ir, stage);
        }
#endif
        
        rc = ir_optimize_func(&ir);
        RC_CH(rc)
#if defined(__DUMP_IR)
        {
            const char* fn_name = ast_name_cstr(be->tree, ir.name_id);
            char stage[256] = { 0 };
            snprintf(stage, sizeof(stage), "after optimization: %s",
                     fn_name ? fn_name : "<unknown>");
            nasm_dump_ir_stage_(ir_dump, be->tree, &ir, stage);
        }
#endif

        nasm_collect_runtime_use(&rt, &ir);

        rc = ir_alloc_run_linear_scan(&ir, &alloc);
        RC_CH(rc)

        rc = nasm_emit_ir_func(be, &ir, &alloc);

cleanup_func:
        ir_alloc_dtor(&alloc);
        ir_func_dtor(&ir);

#if defined(__DUMP_IR)
    if (ir_dump != NULL) {
        fclose(ir_dump);
        ir_dump = NULL;
    }
#endif
        if (rc != OK)
            return rc;
    }

    nasm_emit_runtime(be, &rt);

    be_emitf(be, "section .note.GNU-stack noalloc noexec nowrite progbits\n");

    return OK;
}

static void nasm_emit_header_(backend_t* be)
{
    be_emitf(be, "; BrainrotLang NASM backend, freestanding syscall runtime\n");
    be_emitf(be, "bits 64\n");
    be_emitf(be, "default rel\n\n");
    be_emitf(be, "global _start\n\n");
}

static void nasm_emit_sections_(backend_t* be)
{
    be_emitf(be, "section .rodata\n");
    be_emitf(be, "__f64_sign_mask:    dq 0x8000000000000000\n");
    be_emitf(be, "__f64_zero:         dq 0.0\n");
    be_emitf(be, "__f64_one:          dq 1.0\n");
    be_emitf(be, "__f64_two:          dq 2.0\n");
    be_emitf(be, "__f64_half:         dq 0.5\n");
    be_emitf(be, "__f64_ten:          dq 10.0\n");
    be_emitf(be, "__f64_eps:          dq 0.0000001\n\n");

    be_emitf(be, "section .bss\n");
    be_emitf(be, "__brl_inbuf:        resb 128\n");
    be_emitf(be, "__brl_outbuf:       resb 128\n");
    be_emitf(be, "__brl_ch:           resb 1\n");
    be_emitf(be, "__brl_tmp_i:        resq 1\n");
    be_emitf(be, "__brl_tmp_f:        resq 1\n");
    be_emitf(be, "__brl_unget_ch:     resb 1\n");
    be_emitf(be, "__brl_has_unget:    resb 1\n");

#ifdef __NASM_SIM_GRAPHICS
    be_emitf(be, "__brl_screen:       resb %d\n", BE_SCREEN_BYTES);
#endif

    be_emitf(be, "\nsection .text\n\n");
}
