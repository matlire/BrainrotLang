#include "backend/emitters/nasm_internal.h"


static void  nasm_emit_save_callee_regs_   (backend_t* be);
static void  nasm_emit_restore_callee_regs_(backend_t* be);
static void  nasm_emit_func_epilogue_      (backend_t* be);

static err_t nasm_emit_ir_instr_(backend_t*        be,
                                 const ir_func_t*  f,
                                 const ir_alloc_t* a,
                                 const ir_instr_t* in,
                                 size_t            ip);

static err_t nasm_emit_ir_arith_or_call_(backend_t*        be,
                                         const ir_func_t*  f,
                                         const ir_alloc_t* a,
                                         const ir_instr_t* in,
                                         size_t            ip);


static void nasm_emit_save_callee_regs_(backend_t* be)
{
    be_emitf(be, "    push r12\n");
    be_emitf(be, "    push r13\n");
    be_emitf(be, "    push r14\n");
    be_emitf(be, "    push r15\n");
}

static void nasm_emit_restore_callee_regs_(backend_t* be)
{
    be_emitf(be, "    pop  r15\n");
    be_emitf(be, "    pop  r14\n");
    be_emitf(be, "    pop  r13\n");
    be_emitf(be, "    pop  r12\n");
}

static void nasm_emit_func_epilogue_(backend_t* be)
{
    be_emitf(be, "    mov  rsp, rbp\n");
    nasm_emit_restore_callee_regs_(be);
    be_emitf(be, "    pop  rbp\n");
    be_emitf(be, "    ret\n");
}

void emit_restore_live_regs_around_call(backend_t*        be,
                                        const ir_func_t*  f,
                                        const ir_alloc_t* a,
                                        size_t            ip)
{
    for (size_t v = 0; v < a->vreg_loc_count; ++v)
    {
        ir_alloc_loc_t loc = a->vreg_locs[v];

        if (loc.kind != IR_LOC_REG || !interval_live_across_call(a, v, ip))
            continue;

        if (a->intervals[v].type == IR_TYPE_F64)
        {
            if (!nasm_xmm_preg_is_caller_saved(loc.preg))
                continue;

            be_emitf(be, "    movsd %s, ", NASM_XREG64[loc.preg]);
            emit_call_save_slot(be, f, a, v);
            be_emitf(be, "\n");
            continue;
        }

        if (!nasm_gpr_preg_is_caller_saved(loc.preg))
            continue;

        be_emitf(be, "    mov  %s, ", NASM_PREG64[loc.preg]);
        emit_call_save_slot(be, f, a, v);
        be_emitf(be, "\n");
    }
}

void emit_save_live_regs_around_call(backend_t*        be,
                                     const ir_func_t*  f,
                                     const ir_alloc_t* a,
                                     size_t            ip)
{
    for (size_t v = 0; v < a->vreg_loc_count; ++v)
    {
        ir_alloc_loc_t loc = a->vreg_locs[v];

        if (loc.kind != IR_LOC_REG || !interval_live_across_call(a, v, ip))
            continue;

        if (a->intervals[v].type == IR_TYPE_F64)
        {
            if (!nasm_xmm_preg_is_caller_saved(loc.preg))
                continue;

            be_emitf(be, "    movsd ");
            emit_call_save_slot(be, f, a, v);
            be_emitf(be, ", %s\n", NASM_XREG64[loc.preg]);
            continue;
        }

        if (!nasm_gpr_preg_is_caller_saved(loc.preg))
            continue;

        be_emitf(be, "    mov  ");
        emit_call_save_slot(be, f, a, v);
        be_emitf(be, ", %s\n", NASM_PREG64[loc.preg]);
    }
}

static long incoming_stack_arg_disp_(size_t stack_index)
{
    return (long)(48 + stack_index * NASM_WORD_BYTES);
}

static void emit_incoming_stack_arg_(backend_t* be, size_t stack_index)
{
    be_emitf(be, "[rbp+%ld]", incoming_stack_arg_disp_(stack_index));
}

err_t nasm_emit_ir_func(backend_t* be, const ir_func_t* f, const ir_alloc_t* a)
{
    size_t frame_slots = f->slot_count + a->spill_count + a->vreg_loc_count;
    size_t frame_bytes = align16(frame_slots * NASM_WORD_BYTES);

    be_emitf(be, "%s:\n", f->asm_label);
    be_emitf(be, "    push rbp\n");
    nasm_emit_save_callee_regs_(be);
    be_emitf(be, "    mov  rbp, rsp\n");

    if (frame_bytes)
        be_emitf(be, "    sub  rsp, %zu\n", frame_bytes);

    const func_meta_t* meta = be_find_func(be, f->name_id);

    BE_CHECK(be, meta != NULL, NULL, "Internal: no function metadata");
    BE_CHECK(be, meta->param_count <= IR_MAX_CALL_ARGS,
             NULL, "IR supports max %d params", IR_MAX_CALL_ARGS);

    size_t iarg      = 0;
    size_t farg      = 0;
    size_t stack_arg = 0;

    for (size_t i = 0; i < meta->param_count; ++i)
    {
        if (meta->param_types[i] == AST_TYPE_FLOAT)
        {
            if (farg < NASM_XMM_ARG_REG_COUNT)
            {
                be_emitf(be, "    movsd ");
                emit_slot(be, i);
                be_emitf(be, ", %s\n", NASM_XMM_ARG_REGS[farg++]);
            }
            else
            {
                be_emitf(be, "    movsd xmm0, ");
                emit_incoming_stack_arg_(be, stack_arg++);
                be_emitf(be, "\n");

                be_emitf(be, "    movsd ");
                emit_slot(be, i);
                be_emitf(be, ", xmm0\n");
            }
        }
        else
        {
            if (iarg < NASM_INT_ARG_REG_COUNT)
            {
                be_emitf(be, "    mov  ");
                emit_slot(be, i);
                be_emitf(be, ", %s\n", NASM_ARG_REGS[iarg++]);
            }
            else
            {
                be_emitf(be, "    mov  rax, ");
                emit_incoming_stack_arg_(be, stack_arg++);
                be_emitf(be, "\n");

                be_emitf(be, "    mov  ");
                emit_slot(be, i);
                be_emitf(be, ", rax\n");
            }
        }
    }

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        err_t rc = nasm_emit_ir_instr_(be, f, a, &f->instrs[ip], ip);

        if (rc != OK)
            return rc;
    }

    be_emitf(be, ".L_epilogue_%s:\n", ast_name_cstr(be->tree, f->name_id));
    nasm_emit_func_epilogue_(be);
    be_emitf(be, "\n");

    return OK;
}

static err_t nasm_emit_ir_instr_(backend_t*        be,
                                 const ir_func_t*  f,
                                 const ir_alloc_t* a,
                                 const ir_instr_t* in,
                                 size_t            ip)
{
    (void)ip;

    switch (in->op)
    {
        case IR_OP_LABEL:
            be_emitf(be, ".L%zu:\n", in->label_id);
            return OK;

        case IR_OP_ADD_SLOT_IMM_I64:
            if (in->imm == 1)
            {
                be_emitf(be, "    inc  qword ");
                emit_slot(be, in->slot);
                be_emitf(be, "\n");
            }
            else if (in->imm == -1)
            {
                be_emitf(be, "    dec  qword ");
                emit_slot(be, in->slot);
                be_emitf(be, "\n");
            }
            else
            {
                be_emitf(be, "    add  qword ");
                emit_slot(be, in->slot);
                be_emitf(be, ", %lld\n", (long long)in->imm);
            }
            return OK;

        case IR_OP_JMP:
            be_emitf(be, "    jmp  .L%zu\n", in->label_id);
            return OK;

        case IR_OP_JZ:
            emit_load_vreg(be, f, a, in->a, "rax");
            be_emitf(be, "    test rax, rax\n");
            be_emitf(be, "    jz   .L%zu\n", in->label_id);
            return OK;

        case IR_OP_MOV_IMM_I64:
            emit_mov_imm_i64_to_vreg(be, f, a, in->dst, in->imm);
            return OK;

        case IR_OP_NEG_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            be_emitf(be, "    neg  rax\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_NOT_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            be_emitf(be, "    test rax, rax\n");
            be_emitf(be, "    sete al\n");
            be_emitf(be, "    movzx rax, al\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_JCC_FALSE_I64:
        {
            const char* jcc = jcc_false_i64((token_kind_t)in->imm);

            BE_CHECK(be, jcc != NULL, NULL, "Bad i64 branch condition");

            emit_load_vreg(be, f, a, in->a, "rax");
            emit_load_vreg(be, f, a, in->b, "rdx");

            be_emitf(be, "    cmp  rax, rdx\n");
            be_emitf(be, "    %s  .L%zu\n", jcc, in->label_id);
            return OK;
        }

        case IR_OP_JCC_FALSE_F64:
        {
            const char* jcc = jcc_false_f64((token_kind_t)in->imm);

            BE_CHECK(be, jcc != NULL, NULL, "Bad f64 branch condition");

            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");

            be_emitf(be, "    ucomisd xmm0, xmm1\n");
            be_emitf(be, "    %s  .L%zu\n", jcc, in->label_id);
            return OK;
        }

        case IR_OP_MOV:
            if (in->dst.type == IR_TYPE_F64)
            {
                emit_load_freg(be, f, a, in->a, "xmm0");
                emit_store_freg(be, f, a, in->dst, "xmm0");
            }
            else
            {
                emit_load_vreg(be, f, a, in->a, "rax");
                emit_store_vreg(be, f, a, in->dst, "rax");
            }
            return OK;

        case IR_OP_MOV_IMM_F64:
            emit_mov_imm_f64_to_vreg(be, f, a, in->dst, in->imm);
            return OK;

        case IR_OP_LOAD_SLOT:
            if (in->dst.type == IR_TYPE_F64)
                emit_load_slot_to_vreg_f64(be, f, a, in->dst, in->slot);
            else
                emit_load_slot_to_vreg_i64(be, f, a, in->dst, in->slot);
            return OK;

        case IR_OP_STORE_SLOT:
            if (in->a.type == IR_TYPE_F64)
                emit_store_slot_from_vreg_f64(be, f, a, in->a, in->slot);
            else
                emit_store_slot_from_vreg_i64(be, f, a, in->a, in->slot);
            return OK;

        case IR_OP_NEG_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    xorpd xmm0, [__f64_sign_mask]\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_I64_TO_F64:
            emit_load_vreg(be, f, a, in->a, "rax");
            be_emitf(be, "    cvtsi2sd xmm0, rax\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_F64_TO_I64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    cvttsd2si rax, xmm0\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        default:
            break;
    }

    return nasm_emit_ir_arith_or_call_(be, f, a, in, ip);
}

static err_t nasm_emit_ir_arith_or_call_(backend_t*        be,
                                         const ir_func_t*  f,
                                         const ir_alloc_t* a,
                                         const ir_instr_t* in,
                                         size_t            ip)
{
    switch (in->op)
    {
        case IR_OP_RET_IMM_I64:
            if (in->imm == 0)
                be_emitf(be, "    xor  eax, eax\n");
            else
                be_emitf(be, "    mov  rax, %lld\n", (long long)in->imm);

            be_emitf(be, "    jmp  .L_epilogue_%s\n",
                     ast_name_cstr(be->tree, f->name_id));
            return OK;

        case IR_OP_ADD_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            emit_load_vreg(be, f, a, in->b, "rdx");
            be_emitf(be, "    add  rax, rdx\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_SUB_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            emit_load_vreg(be, f, a, in->b, "rdx");
            be_emitf(be, "    sub  rax, rdx\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_MUL_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            emit_load_vreg(be, f, a, in->b, "rdx");
            be_emitf(be, "    imul rax, rdx\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_DIV_I64:
            emit_load_vreg(be, f, a, in->a, "rax");
            emit_load_vreg(be, f, a, in->b, "rdi");
            be_emitf(be, "    cqo\n");
            be_emitf(be, "    idiv rdi\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_POW_I64:
            emit_load_vreg(be, f, a, in->a, "rdi");
            emit_load_vreg(be, f, a, in->b, "rsi");
            be_emitf(be, "    call __brl_ipow_i64\n");
            emit_store_vreg(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:
            return nasm_emit_cmp(be, f, a, in);

        case IR_OP_SQRT_F64:
            emit_save_live_regs_around_call(be, f, a, ip);
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_sqrt_f64\n");
            emit_restore_live_regs_around_call(be, f, a, ip);
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_POW_F64:
            emit_save_live_regs_around_call(be, f, a, ip);
            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");
            be_emitf(be, "    call __brl_fpow_f64\n");
            emit_restore_live_regs_around_call(be, f, a, ip);
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_CALL:
            return nasm_emit_call(be, f, a, in, ip);

        case IR_OP_RET:
            if (in->a.id != IR_NO_VREG)
            {
                if (in->a.type == IR_TYPE_F64)
                    emit_load_freg(be, f, a, in->a, "xmm0");
                else
                    emit_load_vreg(be, f, a, in->a, "rax");
            }

            be_emitf(be, "    jmp  .L_epilogue_%s\n",
                     ast_name_cstr(be->tree, f->name_id));
            return OK;

        case IR_OP_PRINTF_I64:
            return nasm_emit_printf_i64(be, f, a, in, ip);

        case IR_OP_ADD_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");
            be_emitf(be, "    addsd xmm0, xmm1\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_SUB_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");
            be_emitf(be, "    subsd xmm0, xmm1\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_MUL_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");
            be_emitf(be, "    mulsd xmm0, xmm1\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_DIV_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            emit_load_freg(be, f, a, in->b, "xmm1");
            be_emitf(be, "    divsd xmm0, xmm1\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_CMP_EQ_F64:
        case IR_OP_CMP_NE_F64:
        case IR_OP_CMP_LT_F64:
        case IR_OP_CMP_GT_F64:
        case IR_OP_CMP_LE_F64:
        case IR_OP_CMP_GE_F64:
            return nasm_emit_fcmp(be, f, a, in);

        case IR_OP_FLOOR_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_floor_f64\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_CEIL_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_ceil_f64\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_ROUND_F64:
            emit_load_freg(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_round_f64\n");
            emit_store_freg(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_PRINTF_F64:
            return nasm_emit_printf_f64(be, f, a, in, ip);

        case IR_OP_PUTCHAR_I64:
        case IR_OP_SCANF_I64:
        case IR_OP_SCANF_F64:
        case IR_OP_GETCHAR_I64:
        case IR_OP_RUNTIME_DRAW:
        case IR_OP_RUNTIME_CLEAN:
        case IR_OP_RUNTIME_SET_PIXEL:
            return nasm_emit_runtime_call(be, f, a, in, ip);

        default:
            BE_FAIL_NODE(be, NULL, "NASM IR emit: unsupported op %s", ir_op_to_cstr(in->op));
    }
}
