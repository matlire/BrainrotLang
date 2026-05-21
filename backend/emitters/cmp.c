#include "backend/emitters/nasm_internal.h"

ir_op_t binop_to_ir(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_PLUS:  return IR_OP_ADD_I64;
        case TOK_OP_MINUS: return IR_OP_SUB_I64;
        case TOK_OP_MUL:   return IR_OP_MUL_I64;
        case TOK_OP_DIV:   return IR_OP_DIV_I64;
        case TOK_OP_POW:   return IR_OP_POW_I64;

        case TOK_OP_EQ:    return IR_OP_CMP_EQ_I64;
        case TOK_OP_NEQ:   return IR_OP_CMP_NE_I64;
        case TOK_OP_LT:    return IR_OP_CMP_LT_I64;
        case TOK_OP_GT:    return IR_OP_CMP_GT_I64;
        case TOK_OP_LTE:   return IR_OP_CMP_LE_I64;
        case TOK_OP_GTE:   return IR_OP_CMP_GE_I64;

        default:           return IR_OP_NOP;
    }
}

const char* jcc_false_i64(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_EQ:  return "jne";
        case TOK_OP_NEQ: return "je";
        case TOK_OP_LT:  return "jge";
        case TOK_OP_GT:  return "jle";
        case TOK_OP_LTE: return "jg";
        case TOK_OP_GTE: return "jl";

        default:         return NULL;
    }
}

const char* jcc_false_f64(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_EQ:  return "jne";
        case TOK_OP_NEQ: return "je";
        case TOK_OP_LT:  return "jae";
        case TOK_OP_GT:  return "jbe";
        case TOK_OP_LTE: return "ja";
        case TOK_OP_GTE: return "jb";

        default:         return NULL;
    }
}

err_t nasm_emit_cmp(backend_t*        be,
                    const ir_func_t*  f,
                    const ir_alloc_t* a,
                    const ir_instr_t* in)
{
    const char* setcc = NULL;

    switch (in->op)
    {
        case IR_OP_CMP_EQ_I64: setcc = "sete";  break;
        case IR_OP_CMP_NE_I64: setcc = "setne"; break;
        case IR_OP_CMP_LT_I64: setcc = "setl";  break;
        case IR_OP_CMP_GT_I64: setcc = "setg";  break;
        case IR_OP_CMP_LE_I64: setcc = "setle"; break;
        case IR_OP_CMP_GE_I64: setcc = "setge"; break;

        default: return ERR_BAD_ARG;
    }

    emit_load_vreg(be, f, a, in->a, "rax");
    emit_load_vreg(be, f, a, in->b, "rdx");

    be_emitf(be, "    cmp  rax, rdx\n");
    be_emitf(be, "    %s al\n", setcc);
    be_emitf(be, "    movzx rax, al\n");

    emit_store_vreg(be, f, a, in->dst, "rax");
    return OK;
}

err_t nasm_emit_fcmp(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     const ir_instr_t* in)
{
    const char* setcc = NULL;

    switch (in->op)
    {
        case IR_OP_CMP_EQ_F64: setcc = "sete";  break;
        case IR_OP_CMP_NE_F64: setcc = "setne"; break;
        case IR_OP_CMP_LT_F64: setcc = "setb";  break;
        case IR_OP_CMP_GT_F64: setcc = "seta";  break;
        case IR_OP_CMP_LE_F64: setcc = "setbe"; break;
        case IR_OP_CMP_GE_F64: setcc = "setae"; break;

        default: return ERR_BAD_ARG;
    }

    emit_load_freg(be, f, a, in->a, "xmm0");
    emit_load_freg(be, f, a, in->b, "xmm1");

    be_emitf(be, "    ucomisd xmm0, xmm1\n");
    be_emitf(be, "    %s al\n", setcc);
    be_emitf(be, "    movzx rax, al\n");

    emit_store_vreg(be, f, a, in->dst, "rax");
    return OK;
}
