#include "backend_internal.h"
#include "backend/ir/backend_ir.h"
#define BE_SCREEN_STRIDE (BE_SCREEN_WIDTH + 1)
#define BE_SCREEN_BYTES  (BE_SCREEN_STRIDE * BE_SCREEN_HEIGHT)

#define NASM_WORD_BYTES  8
#define NASM_STACK_ALIGN 16

#define NASM_INT_ARG_REG_COUNT 6
#define NASM_XMM_ARG_REG_COUNT 8

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
static const char* NASM_ARG_REGS[NASM_INT_ARG_REG_COUNT] =
{
    "rdi", "rsi", "rdx", "rcx", "r8", "r9"
};

static const char* NASM_XMM_ARG_REGS[NASM_XMM_ARG_REG_COUNT] =
{
    "xmm0", "xmm1", "xmm2", "xmm3",
    "xmm4", "xmm5", "xmm6", "xmm7"
};

static const char* NASM_PREG64[NASM_PREG_COUNT] =
{
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
};

static const char* NASM_XREG64[NASM_PREG_COUNT] =
{
    "xmm8",
    "xmm9",
    "xmm10",
    "xmm11",
    "xmm12",
    "xmm13",
};
static int nasm_gpr_preg_is_caller_saved_(int preg)
{
    return preg == 0 || preg == 1; // r10, r11
}

static int nasm_xmm_preg_is_caller_saved_(int preg)
{
    (void)preg;
    return 1;
}
static size_t align16_(size_t x)
{
    return (x + 15u) & ~15u;
}

static i64_t f64_bits_(double x)
{
    union
    {
        double d;
        i64_t  i;
    } u = { .d = x };

    return u.i;
}

typedef struct
{
    backend_t* be;
    ir_func_t* f;

    const func_meta_t* meta;

    size_t ret_label;

    size_t* loop_end_labels;
    size_t  loop_amount;
    size_t  loop_cap;
} nasm_lower_t;

static err_t nasm_emit_program_(backend_t* be, const ast_node_t* program);
static err_t nasm_emit_c_main_ (backend_t* be, const ast_node_t* program);

static err_t nasm_lower_func_         (backend_t* be, const ast_node_t* fn, ir_func_t* out);
static err_t nasm_lower_stmt_         (nasm_lower_t* l, const ast_node_t* st);
static err_t nasm_lower_block_        (nasm_lower_t* l, const ast_node_t* block);
static err_t nasm_lower_vdecl_        (nasm_lower_t* l, const ast_node_t* vd);
static err_t nasm_lower_assign_       (nasm_lower_t* l, const ast_node_t* asn);
static err_t nasm_lower_return_       (nasm_lower_t* l, const ast_node_t* ret);
static err_t nasm_lower_expr_stmt_    (nasm_lower_t* l, const ast_node_t* st);
static err_t nasm_lower_call_stmt_    (nasm_lower_t* l, const ast_node_t* st);
static err_t nasm_lower_print_i64_    (nasm_lower_t* l, const ast_node_t* pr);
static err_t nasm_lower_if_           (nasm_lower_t* l, const ast_node_t* ifn);
static err_t nasm_lower_while_        (nasm_lower_t* l, const ast_node_t* w);
static err_t nasm_lower_break_        (nasm_lower_t* l, const ast_node_t* brk);
static err_t nasm_lower_if_tail_(nasm_lower_t* l, const ast_node_t* tail, size_t L_end);

static err_t nasm_lower_expr_i64_     (nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_unary_i64_    (nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_binary_i64_   (nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_call_expr_i64_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);

static err_t nasm_emit_ir_func_         (backend_t* be, const ir_func_t* f, const ir_alloc_t* a);
static err_t nasm_emit_ir_instr_        (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static err_t nasm_emit_ir_arith_or_call_(backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static err_t nasm_emit_cmp_             (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in);
static err_t nasm_emit_call_            (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static err_t nasm_emit_printf_i64_      (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static void emit_save_live_regs_around_call_   (backend_t* be, const ir_func_t* f,
                                                const ir_alloc_t* a, size_t ip);
static void emit_restore_live_regs_around_call_(backend_t* be, const ir_func_t* f,
                                                const ir_alloc_t* a, size_t ip);

static err_t nasm_lower_expr_f64_      (nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_call_expr_f64_ (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
static err_t nasm_lower_builtin_i64_   (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
static err_t nasm_lower_builtin_f64_   (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
static err_t nasm_lower_print_f64_     (nasm_lower_t* l, const ast_node_t* pr);
static err_t nasm_lower_user_call_     (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
static err_t nasm_emit_printf_f64_     (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static err_t nasm_emit_runtime_call_   (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in, size_t ip);
static err_t nasm_emit_fcmp_           (backend_t* be, const ir_func_t* f, const ir_alloc_t* a,
                                         const ir_instr_t* in);


static void nasm_emit_runtime_(backend_t* be, const nasm_runtime_use_t* rt);

static err_t nasm_lower_logical_and_i64_(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_logical_or_i64_ (nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
static err_t nasm_lower_expr_f64_to_i64_(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out);
/*
    IR
*/

static err_t nasm_lower_print_f64_(nasm_lower_t* l, const ast_node_t* pr)
{
    ir_vreg_t val = { 0 };

    err_t rc = nasm_lower_expr_f64_(l, pr->left, &val);
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_PRINTF_F64,
        .a   = val,
        .pos = pr->pos,
    });
}

static void emit_call_save_slot_(backend_t* be,
                                 const ir_func_t* f,
                                 const ir_alloc_t* a,
                                 size_t vreg)
{
    size_t slot = f->slot_count + a->spill_count + vreg;

    be_emitf(be, "[rbp%ld]", -(long)((slot + 1) * NASM_WORD_BYTES));
}

static int interval_live_across_call_(const ir_alloc_t* a,
                                      size_t            vreg_id,
                                      size_t            ip)
{
    if (!a || vreg_id >= a->interval_count)
        return 0;

    const ir_interval_t* it = &a->intervals[vreg_id];

    return it->first != SIZE_MAX &&
           it->first < ip &&
           it->last  > ip;
}
static long slot_disp_(size_t slot)
{
    return -(long)((slot + 1) * NASM_WORD_BYTES);
}

static long spill_disp_(const ir_func_t* f, size_t spill)
{
    return -(long)((f->slot_count + spill + 1) * NASM_WORD_BYTES);
}

static void emit_slot_(backend_t* be, size_t slot)
{
    be_emitf(be, "[rbp%ld]", slot_disp_(slot));
}

static void emit_spill_(backend_t* be, const ir_func_t* f, size_t spill)
{
    be_emitf(be, "[rbp%ld]", spill_disp_(f, spill));
}

static int ast_func_body_ends_with_return_(const ast_node_t* body)
{
    if (!body || body->kind != ASTK_BLOCK)
        return 0;

    const ast_node_t* last = body->left;
    if (!last)
        return 0;

    while (last->right)
        last = last->right;

    return last->kind == ASTK_RETURN;
}

static err_t nasm_lower_func_(backend_t* be, const ast_node_t* fn, ir_func_t* out)
{
    const func_meta_t* meta = be_find_func(be, fn->u.func.name_id);
    BE_CHECK(be, meta != NULL, fn, "Internal: missing function metadata");

    err_t rc = ir_func_ctor(out, meta->name_id, meta->label, meta->ret_type);
    if (rc != OK)
        return rc;

    be->cur_fn = meta;
    be->bind_amount = 0;
    be->scope_depth = 1;
    be->next_local_offset = 0;

    nasm_lower_t l = {
        .be        = be,
        .f         = out,
        .meta      = meta,
        .ret_label = ir_new_label(out),
    };

    const ast_node_t* plist = fn->left;
    const ast_node_t* body  = plist ? plist->right : NULL;

    BE_CHECK(be, plist && plist->kind == ASTK_PARAM_LIST, fn, "FUNC missing PARAM_LIST");
    BE_CHECK(be, body != NULL, fn, "FUNC missing body");

    size_t param_i = 0;

    for (const ast_node_t* p = plist->left; p; p = p->right, ++param_i)
    {
        BE_CHECK(be,
                 p->u.param.type == AST_TYPE_INT ||
                 p->u.param.type == AST_TYPE_PTR ||
                 p->u.param.type == AST_TYPE_FLOAT,
                 p, "NASM supports only int/ptr/float params");

        size_t slot = 0;
        rc = ir_add_slot(out, p->u.param.name_id, p->u.param.type, &slot);
        if (rc != OK)
            return rc;

        rc = be_bind_push(be, p->u.param.name_id, p->u.param.type, slot, be->scope_depth);
        if (rc != OK)
            return rc;
    }
    rc = nasm_lower_stmt_(&l, body);
if (rc != OK)
{
    free(l.loop_end_labels);
    return rc;
}

   const int need_default_ret =
    meta->ret_type == AST_TYPE_VOID ||
    !ast_func_body_ends_with_return_(body);

if (need_default_ret)
{
    rc = ir_emit(out, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = l.ret_label,
    });
    if (rc != OK)
        return rc;

    if (meta->ret_type == AST_TYPE_VOID)
    {
        rc = ir_emit(out, (ir_instr_t){ .op = IR_OP_RET });
    }
    else if (meta->ret_type == AST_TYPE_FLOAT)
    {
        ir_vreg_t z = ir_new_vreg(out, IR_TYPE_F64);

        rc = ir_emit(out, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_F64,
            .dst = z,
            .imm = f64_bits_(0.0),
        });
        if (rc != OK)
            return rc;

        rc = ir_emit(out, (ir_instr_t){
            .op = IR_OP_RET,
            .a  = z,
        });
    }
    else
    {
        ir_vreg_t z = ir_new_vreg(out, IR_TYPE_I64);

        rc = ir_emit(out, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_I64,
            .dst = z,
            .imm = 0,
        });
        if (rc != OK)
            return rc;

        rc = ir_emit(out, (ir_instr_t){
            .op = IR_OP_RET,
            .a  = z,
        });
    }

    if (rc != OK)
        return rc;
}
    if (meta->ret_type == AST_TYPE_VOID)
    {
        rc = ir_emit(out, (ir_instr_t){ .op = IR_OP_RET });
    }
    else if (meta->ret_type == AST_TYPE_FLOAT)
    {
        ir_vreg_t z = ir_new_vreg(out, IR_TYPE_F64);

        rc = ir_emit(out, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_F64,
            .dst = z,
            .imm = f64_bits_(0.0),
        });
        if (rc != OK)
            return rc;

        rc = ir_emit(out, (ir_instr_t){
            .op = IR_OP_RET,
            .a  = z,
        });
    }
    else
    {
        ir_vreg_t z = ir_new_vreg(out, IR_TYPE_I64);

        rc = ir_emit(out, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_I64,
            .dst = z,
            .imm = 0,
        });
        if (rc != OK)
            return rc;

        rc = ir_emit(out, (ir_instr_t){
            .op = IR_OP_RET,
            .a  = z,
        });
    }

    out->frame_slots = out->slot_count;
    free(l.loop_end_labels);

    return rc;
}
static err_t nasm_lower_stmt_(nasm_lower_t* l, const ast_node_t* st)
{
    if (!st)
        return OK;

    switch (st->kind)
    {
        case ASTK_BLOCK:     return nasm_lower_block_(l, st);
        case ASTK_VAR_DECL:  return nasm_lower_vdecl_(l, st);
        case ASTK_ASSIGN:    return nasm_lower_assign_(l, st);
        case ASTK_RETURN:    return nasm_lower_return_(l, st);
        case ASTK_EXPR_STMT: return nasm_lower_expr_stmt_(l, st);
        case ASTK_CALL_STMT: return nasm_lower_call_stmt_(l, st);
        case ASTK_IF:        return nasm_lower_if_(l, st);
        case ASTK_WHILE:     return nasm_lower_while_(l, st);
        case ASTK_BREAK:     return nasm_lower_break_(l, st);

        case ASTK_COUT:
        case ASTK_ICOUT:
            return nasm_lower_print_i64_(l, st);

        case ASTK_FCOUT:
            return nasm_lower_print_f64_(l, st);
        default:
            BE_FAIL_NODE(l->be, st, "NASM IR: unsupported statement %s",
                         ast_kind_to_cstr(st->kind));
    }
}

static size_t ast_arg_count_(const ast_node_t* args);
static const ast_node_t* arg_at_(const ast_node_t* args, size_t idx);
static int ast_i64_literal_(const ast_node_t* n, i64_t* out)
{
    if (!n || n->kind != ASTK_NUM_LIT || n->u.num.lit_type != LIT_INT)
        return 0;

    if (out)
        *out = n->u.num.lit.i64;

    return 1;
}
static err_t nasm_lower_builtin_f64_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    const ast_node_t* args = call->left;
    size_t argc = ast_arg_count_(args);

    if (be_streq(name, "fin") || be_streq(name, "nocap"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);
        err_t rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_SCANF_F64, .dst = dst, .pos = call->pos });
        if (rc != OK) return rc;

        *out = dst;
        return OK;
    }

    if (be_streq(name, "fout") || be_streq(name, "rizz"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        ir_vreg_t v = { 0 };
        err_t rc = nasm_lower_expr_f64_(l, arg_at_(args, 0), &v);
        if (rc != OK) return rc;

        rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_PRINTF_F64, .a = v, .pos = call->pos });
        if (rc != OK) return rc;

        *out = v;
        return OK;
    }

    if (be_streq(name, "sqrt"))
         {
        BE_CHECK(l->be, argc == 1, call, "sqrt(x) takes 1 arg");

        ir_vreg_t x = { 0 };
        err_t rc = nasm_lower_expr_f64_(l, arg_at_(args, 0), &x);
        if (rc != OK) return rc;

        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

        rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_SQRT_F64,
            .dst = dst,
            .a   = x,
            .pos = call->pos,
        });
        if (rc != OK) return rc;

        *out = dst;
        return OK;
    }

    if (be_streq(name, "pow") || be_streq(name, "mpow"))
{
    BE_CHECK(l->be, argc == 2, call, "%s(x,y) takes 2 args", name);

    const ast_node_t* base = arg_at_(args, 0);
    const ast_node_t* exp  = arg_at_(args, 1);

    i64_t exp_i = 0;
    if (ast_i64_literal_(exp, &exp_i) && exp_i >= 0 && exp_i <= 4)
    {
        if (exp_i == 0)
        {
            ir_vreg_t one = ir_new_vreg(l->f, IR_TYPE_F64);

            err_t rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_MOV_IMM_F64,
                .dst = one,
                .imm = f64_bits_(1.0),
                .pos = call->pos,
            });
            if (rc != OK)
                return rc;

            *out = one;
            return OK;
        }

        ir_vreg_t x = { 0 };
        err_t rc = nasm_lower_expr_f64_(l, base, &x);
        if (rc != OK)
            return rc;

        if (exp_i == 1)
        {
            *out = x;
            return OK;
        }

        ir_vreg_t cur = x;

        for (i64_t i = 1; i < exp_i; ++i)
        {
            ir_vreg_t next = ir_new_vreg(l->f, IR_TYPE_F64);

            rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_MUL_F64,
                .dst = next,
                .a   = cur,
                .b   = x,
                .pos = call->pos,
            });
            if (rc != OK)
                return rc;

            cur = next;
        }

        *out = cur;
        return OK;
    }

    ir_vreg_t x = { 0 };
    ir_vreg_t y = { 0 };

    err_t rc = nasm_lower_expr_f64_(l, base, &x);
    if (rc != OK)
        return rc;

    rc = nasm_lower_expr_f64_(l, exp, &y);
    if (rc != OK)
        return rc;

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_POW_F64,
        .dst = dst,
        .a   = x,
        .b   = y,
        .pos = call->pos,
    });
    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}
    ir_vreg_t iv = { 0 };
    err_t rc = nasm_lower_builtin_i64_(l, call, &iv);
    if (rc != OK) return rc;

    ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_I64_TO_F64,
        .dst = fv,
        .a   = iv,
        .pos = call->pos,
    });
    if (rc != OK) return rc;

    *out = fv;
    return OK;
}
static err_t nasm_lower_block_(nasm_lower_t* l, const ast_node_t* block)
{
    l->be->scope_depth++;
    size_t depth = l->be->scope_depth;

    for (const ast_node_t* c = block->left; c; c = c->right)
    {
        err_t rc = nasm_lower_stmt_(l, c);
        if (rc != OK)
            return rc;
    }

    be_bind_pop_depth(l->be, depth);
    l->be->scope_depth--;

    return OK;
}

static err_t nasm_lower_vdecl_(nasm_lower_t* l, const ast_node_t* vd)
{
    BE_CHECK(l->be,
             vd->u.vdecl.type == AST_TYPE_INT ||
             vd->u.vdecl.type == AST_TYPE_PTR ||
             vd->u.vdecl.type == AST_TYPE_FLOAT,
             vd, "supports only int/ptr/float locals");

    size_t slot = 0;
    err_t rc = ir_add_slot(l->f, vd->u.vdecl.name_id, vd->u.vdecl.type, &slot);
    if (rc != OK)
        return rc;

    rc = be_bind_push(l->be, vd->u.vdecl.name_id, vd->u.vdecl.type, slot, l->be->scope_depth);
    if (rc != OK)
        return rc;

    ir_vreg_t val = { .id = IR_NO_VREG, .type = IR_TYPE_VOID };

    if (vd->left)
    {
        if (vd->u.vdecl.type == AST_TYPE_FLOAT)
            rc = nasm_lower_expr_f64_(l, vd->left, &val);
        else
            rc = nasm_lower_expr_i64_(l, vd->left, &val);

        if (rc != OK)
            return rc;
    }
    else if (vd->u.vdecl.type == AST_TYPE_FLOAT)
    {
        val = ir_new_vreg(l->f, IR_TYPE_F64);

        rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_F64,
            .dst = val,
            .imm = 0,
            .pos = vd->pos,
        });
        if (rc != OK)
            return rc;
    }
    else
    {
        val = ir_new_vreg(l->f, IR_TYPE_I64);

        rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_I64,
            .dst = val,
            .imm = 0,
            .pos = vd->pos,
        });
        if (rc != OK)
            return rc;
    }

    return ir_emit(l->f, (ir_instr_t){
        .op   = IR_OP_STORE_SLOT,
        .slot = slot,
        .a    = val,
        .pos  = vd->pos,
    });
}
static int nasm_match_self_add_imm_(const ast_node_t* asn,
                                    size_t            name_id,
                                    i64_t*            out_imm)
{
    if (!asn || asn->kind != ASTK_ASSIGN || !asn->left)
        return 0;

    const ast_node_t* e = asn->left;

    if (e->kind != ASTK_BINARY)
        return 0;

    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    if (!lhs || !rhs)
        return 0;

    if (e->u.binary.op == TOK_OP_PLUS)
    {
        if (lhs->kind == ASTK_IDENT &&
            lhs->u.ident.name_id == name_id &&
            rhs->kind == ASTK_NUM_LIT &&
            rhs->u.num.lit_type == LIT_INT)
        {
            *out_imm = rhs->u.num.lit.i64;
            return 1;
        }

        if (rhs->kind == ASTK_IDENT &&
            rhs->u.ident.name_id == name_id &&
            lhs->kind == ASTK_NUM_LIT &&
            lhs->u.num.lit_type == LIT_INT)
        {
            *out_imm = lhs->u.num.lit.i64;
            return 1;
        }
    }

    if (e->u.binary.op == TOK_OP_MINUS)
    {
        if (lhs->kind == ASTK_IDENT &&
            lhs->u.ident.name_id == name_id &&
            rhs->kind == ASTK_NUM_LIT &&
            rhs->u.num.lit_type == LIT_INT)
        {
            *out_imm = -rhs->u.num.lit.i64;
            return 1;
        }
    }

    return 0;
}
static err_t nasm_lower_assign_(nasm_lower_t* l, const ast_node_t* asn)
{
    ssize_t bi = be_bind_lookup(l->be, asn->u.assign.name_id);
    BE_CHECK(l->be, bi >= 0, asn, "Assignment to unknown variable");

    const binding_t* b = &l->be->binds[(size_t)bi];
if (b->type == AST_TYPE_INT || b->type == AST_TYPE_PTR)
{
    i64_t imm = 0;

    if (nasm_match_self_add_imm_(asn, asn->u.assign.name_id, &imm))
    {
        return ir_emit(l->f, (ir_instr_t){
            .op   = IR_OP_ADD_SLOT_IMM_I64,
            .slot = b->offset,
            .imm  = imm,
            .pos  = asn->pos,
        });
    }
}
    ir_vreg_t val = { 0 };

    err_t rc = OK;
    if (b->type == AST_TYPE_FLOAT)
        rc = nasm_lower_expr_f64_(l, asn->left, &val);
    else
        rc = nasm_lower_expr_i64_(l, asn->left, &val);

    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op   = IR_OP_STORE_SLOT,
        .slot = b->offset,
        .a    = val,
        .pos  = asn->pos,
    });
}

static err_t nasm_lower_return_(nasm_lower_t* l, const ast_node_t* ret)
{
if (l->meta->ret_type == AST_TYPE_INT ||
    l->meta->ret_type == AST_TYPE_PTR)
{
    i64_t imm = 0;

    if (ast_i64_literal_(ret->left, &imm))
    {
        return ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_RET_IMM_I64,
            .imm = imm,
            .pos = ret->pos,
        });
    }
}

    if (ast_type_is_void(l->meta->ret_type))
    {
        return ir_emit(l->f, (ir_instr_t){
            .op       = IR_OP_JMP,
            .label_id = l->ret_label,
            .pos      = ret->pos,
        });
    }

    ir_vreg_t val = { 0 };

    err_t rc = OK;
    if (l->meta->ret_type == AST_TYPE_FLOAT)
        rc = nasm_lower_expr_f64_(l, ret->left, &val);
    else
        rc = nasm_lower_expr_i64_(l, ret->left, &val);

    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_RET,
        .a   = val,
        .pos = ret->pos,
    });
}

static err_t nasm_lower_print_i64_(nasm_lower_t* l, const ast_node_t* pr)
{
    ir_vreg_t val = { 0 };

    err_t rc = nasm_lower_expr_i64_(l, pr->left, &val);
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_PRINTF_I64,
        .a   = val,
        .pos = pr->pos,
    });
}

static int nasm_is_bool_op_(token_kind_t op)
{
    return op == TOK_OP_EQ  ||
           op == TOK_OP_NEQ ||
           op == TOK_OP_LT  ||
           op == TOK_OP_GT  ||
           op == TOK_OP_LTE ||
           op == TOK_OP_GTE ||
           op == TOK_OP_AND ||
           op == TOK_OP_OR;
}

static ast_type_t nasm_infer_expr_type_(backend_t* be, const ast_node_t* e)
{
    if (!be || !e)
        return AST_TYPE_UNKNOWN;

    if (e->type != AST_TYPE_UNKNOWN)
        return e->type;

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
            return (e->u.num.lit_type == LIT_FLOAT) ? AST_TYPE_FLOAT : AST_TYPE_INT;

        case ASTK_IDENT:
        {
            ssize_t idx = be_bind_lookup(be, e->u.ident.name_id);
            return (idx >= 0) ? be->binds[(size_t)idx].type : AST_TYPE_UNKNOWN;
        }

        case ASTK_CALL:
        {
            const char* name = ast_name_cstr(be->tree, e->u.call.name_id);
            if (!name)
                return AST_TYPE_UNKNOWN;

            if (be_streq(name, "in")  || be_streq(name, "cap") ||
                be_streq(name, "cin") || be_streq(name, "stinky"))
                return AST_TYPE_INT;

            if (be_streq(name, "fin") || be_streq(name, "nocap"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "out")  || be_streq(name, "pookie") ||
                be_streq(name, "cout") || be_streq(name, "menace"))
                return AST_TYPE_INT;

            if (be_streq(name, "fout") || be_streq(name, "rizz"))
                return AST_TYPE_FLOAT;

            if (be_streq(name, "draw")     || be_streq(name, "gyat") ||
                be_streq(name, "clean_vm") || be_streq(name, "skibidi") ||
                be_streq(name, "set_pixel"))
                return AST_TYPE_VOID;

            if (be_streq(name, "fin")  || be_streq(name, "nocap") ||
    be_streq(name, "fout") || be_streq(name, "rizz")  ||
    be_streq(name, "sqrt") || be_streq(name, "pow")   ||
    be_streq(name, "mpow"))
{
    return AST_TYPE_FLOAT;
}

            const func_meta_t* fm = be_find_func(be, e->u.call.name_id);
            return fm ? fm->ret_type : AST_TYPE_UNKNOWN;
        }

        case ASTK_BUILTIN_UNARY:
            return (e->u.builtin_unary.id == AST_BUILTIN_FTOI) ? AST_TYPE_INT : AST_TYPE_FLOAT;

        case ASTK_UNARY:
            if (e->u.unary.op == TOK_OP_NOT)
                return AST_TYPE_INT;
            return nasm_infer_expr_type_(be, e->left);

        case ASTK_BINARY:
        {
            if (nasm_is_bool_op_(e->u.binary.op))
                return AST_TYPE_INT;

            const ast_node_t* lhs = e->left;
            const ast_node_t* rhs = lhs ? lhs->right : NULL;

            ast_type_t lt = nasm_infer_expr_type_(be, lhs);
            ast_type_t rt = nasm_infer_expr_type_(be, rhs);

            if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT)
                return AST_TYPE_FLOAT;

            if (lt == AST_TYPE_UNKNOWN || rt == AST_TYPE_UNKNOWN)
                return AST_TYPE_UNKNOWN;

            return AST_TYPE_INT;
        }

        default:
            return AST_TYPE_UNKNOWN;
    }
}


static err_t nasm_lower_expr_i64_(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    BE_CHECK(l->be, e != NULL, e, "Null expression");

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
        {
            if (e->u.num.lit_type == LIT_FLOAT)
            {
                ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

                err_t rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_MOV_IMM_F64,
                    .dst = fv,
                    .imm = f64_bits_(e->u.num.lit.f64),
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_F64_TO_I64,
                    .dst = iv,
                    .a   = fv,
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                *out = iv;
                return OK;
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

            err_t rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_MOV_IMM_I64,
                .dst = dst,
                .imm = e->u.num.lit.i64,
                .pos = e->pos,
            });

            if (rc != OK)
                return rc;

            *out = dst;
            return OK;
        }
        case ASTK_IDENT:
        {
            ssize_t bi = be_bind_lookup(l->be, e->u.ident.name_id);
            BE_CHECK(l->be, bi >= 0, e, "Unknown variable");

            const binding_t* b = &l->be->binds[(size_t)bi];

            if (b->type == AST_TYPE_FLOAT)
            {
                ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

                err_t rc = ir_emit(l->f, (ir_instr_t){
                    .op   = IR_OP_LOAD_SLOT,
                    .dst  = fv,
                    .slot = b->offset,
                    .pos  = e->pos,
                });
                if (rc != OK) return rc;

                rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_F64_TO_I64,
                    .dst = iv,
                    .a   = fv,
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                *out = iv;
                return OK;
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

            err_t rc = ir_emit(l->f, (ir_instr_t){
                .op = IR_OP_LOAD_SLOT,
                .dst = dst,
                .slot = b->offset,
                .pos = e->pos,
            });

            if (rc != OK)
                return rc;

            *out = dst;
            return OK;
        }

        case ASTK_UNARY:
            return nasm_lower_unary_i64_(l, e, out);

        case ASTK_BINARY:
            return nasm_lower_binary_i64_(l, e, out);

        case ASTK_CALL:
            return nasm_lower_call_expr_i64_(l, e, out);

        case ASTK_BUILTIN_UNARY:
        {
            const ast_node_t* arg = e->left;

            if (e->u.builtin_unary.id == AST_BUILTIN_FTOI)
            {
                ir_vreg_t fv = { 0 };
                err_t rc = nasm_lower_expr_f64_(l, arg, &fv);
                if (rc != OK) return rc;

                ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

                rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_F64_TO_I64,
                    .dst = iv,
                    .a   = fv,
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                *out = iv;
                return OK;
            }

            ir_vreg_t fv = { 0 };
            err_t rc = nasm_lower_expr_f64_(l, e, &fv);
            if (rc != OK) return rc;

            ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);
            rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_F64_TO_I64,
                .dst = iv,
                .a   = fv,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = iv;
            return OK;
        }
        default:
            BE_FAIL_NODE(l->be, e, "IR unsupported expression %s",
                         ast_kind_to_cstr(e->kind));
    }
}

static ir_op_t binop_to_ir_f64_(token_kind_t op)
{
    switch (op)
    {
        case TOK_OP_PLUS:  return IR_OP_ADD_F64;
        case TOK_OP_MINUS: return IR_OP_SUB_F64;
        case TOK_OP_MUL:   return IR_OP_MUL_F64;
        case TOK_OP_DIV:   return IR_OP_DIV_F64;
        case TOK_OP_POW:   return IR_OP_POW_F64;

        case TOK_OP_EQ:    return IR_OP_CMP_EQ_F64;
        case TOK_OP_NEQ:   return IR_OP_CMP_NE_F64;
        case TOK_OP_LT:    return IR_OP_CMP_LT_F64;
        case TOK_OP_GT:    return IR_OP_CMP_GT_F64;
        case TOK_OP_LTE:   return IR_OP_CMP_LE_F64;
        case TOK_OP_GTE:   return IR_OP_CMP_GE_F64;

        default:           return IR_OP_NOP;
    }
}
static err_t nasm_lower_expr_f64_(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    BE_CHECK(l->be, e != NULL, e, "Null expression");

    switch (e->kind)
    {
        case ASTK_NUM_LIT:
        {
            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

            i64_t bits = 0;
            if (e->u.num.lit_type == LIT_FLOAT)
                bits = f64_bits_(e->u.num.lit.f64);
            else
                bits = f64_bits_((double)e->u.num.lit.i64);

            err_t rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_MOV_IMM_F64,
                .dst = dst,
                .imm = bits,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = dst;
            return OK;
        }

        case ASTK_IDENT:
        {
            ssize_t bi = be_bind_lookup(l->be, e->u.ident.name_id);
            BE_CHECK(l->be, bi >= 0, e, "Unknown variable");

            const binding_t* b = &l->be->binds[(size_t)bi];

            if (b->type == AST_TYPE_FLOAT)
            {
                ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

                err_t rc = ir_emit(l->f, (ir_instr_t){
                    .op   = IR_OP_LOAD_SLOT,
                    .dst  = dst,
                    .slot = b->offset,
                    .pos  = e->pos,
                });
                if (rc != OK) return rc;

                *out = dst;
                return OK;
            }

            ir_vreg_t iv = { 0 };
            err_t rc = nasm_lower_expr_i64_(l, e, &iv);
            if (rc != OK) return rc;

            ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);

            rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_I64_TO_F64,
                .dst = fv,
                .a   = iv,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = fv;
            return OK;
        }

        case ASTK_UNARY:
        {
            if (e->u.unary.op == TOK_OP_PLUS)
                return nasm_lower_expr_f64_(l, e->left, out);

            if (e->u.unary.op == TOK_OP_MINUS)
            {
                ir_vreg_t a = { 0 };
                err_t rc = nasm_lower_expr_f64_(l, e->left, &a);
                if (rc != OK) return rc;

                ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

                rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_NEG_F64,
                    .dst = dst,
                    .a   = a,
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                *out = dst;
                return OK;
            }

            BE_FAIL_NODE(l->be, e, "Unsupported float unary operator");
        }

        case ASTK_BINARY:
        {
            const ast_node_t* lhs = e->left;
            const ast_node_t* rhs = lhs ? lhs->right : NULL;
            BE_CHECK(l->be, lhs && rhs, e, "Bad binary node");

            ir_op_t op = binop_to_ir_f64_(e->u.binary.op);
            BE_CHECK(l->be, op != IR_OP_NOP, e, "Unsupported float binary op");

            ir_vreg_t a = { 0 };
            ir_vreg_t b = { 0 };

            err_t rc = nasm_lower_expr_f64_(l, lhs, &a);
            if (rc != OK) return rc;

            rc = nasm_lower_expr_f64_(l, rhs, &b);
            if (rc != OK) return rc;

            ir_type_t dst_type = nasm_is_bool_op_(e->u.binary.op) ? IR_TYPE_I64 : IR_TYPE_F64;
            ir_vreg_t dst = ir_new_vreg(l->f, dst_type);

            rc = ir_emit(l->f, (ir_instr_t){
                .op  = op,
                .dst = dst,
                .a   = a,
                .b   = b,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            if (dst_type == IR_TYPE_F64)
            {
                *out = dst;
                return OK;
            }

            ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
            rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_I64_TO_F64,
                .dst = fv,
                .a   = dst,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = fv;
            return OK;
        }

        case ASTK_CALL:
            return nasm_lower_call_expr_f64_(l, e, out);

        case ASTK_BUILTIN_UNARY:
        {
            const ast_node_t* arg = e->left;

            if (e->u.builtin_unary.id == AST_BUILTIN_ITOF)
            {
                ir_vreg_t iv = { 0 };
                err_t rc = nasm_lower_expr_i64_(l, arg, &iv);
                if (rc != OK) return rc;

                ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
                rc = ir_emit(l->f, (ir_instr_t){
                    .op  = IR_OP_I64_TO_F64,
                    .dst = fv,
                    .a   = iv,
                    .pos = e->pos,
                });
                if (rc != OK) return rc;

                *out = fv;
                return OK;
            }

            ir_vreg_t fv = { 0 };
            err_t rc = nasm_lower_expr_f64_(l, arg, &fv);
            if (rc != OK) return rc;

            ir_op_t op = IR_OP_NOP;

            switch (e->u.builtin_unary.id)
            {
                case AST_BUILTIN_FLOOR: op = IR_OP_FLOOR_F64; break;
                case AST_BUILTIN_CEIL:  op = IR_OP_CEIL_F64;  break;
                case AST_BUILTIN_ROUND: op = IR_OP_ROUND_F64; break;
                case AST_BUILTIN_FTOI:
                {
                    ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);
                    rc = ir_emit(l->f, (ir_instr_t){
                        .op  = IR_OP_F64_TO_I64,
                        .dst = iv,
                        .a   = fv,
                        .pos = e->pos,
                    });
                    if (rc != OK) return rc;

                    ir_vreg_t out_f = ir_new_vreg(l->f, IR_TYPE_F64);
                    rc = ir_emit(l->f, (ir_instr_t){
                        .op  = IR_OP_I64_TO_F64,
                        .dst = out_f,
                        .a   = iv,
                        .pos = e->pos,
                    });
                    if (rc != OK) return rc;

                    *out = out_f;
                    return OK;
                }

                default:
                    BE_FAIL_NODE(l->be, e, "Unsupported float builtin");
            }

            ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_F64);

            rc = ir_emit(l->f, (ir_instr_t){
                .op  = op,
                .dst = dst,
                .a   = fv,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = dst;
            return OK;
        }

        default:
        {
            ir_vreg_t iv = { 0 };
            err_t rc = nasm_lower_expr_i64_(l, e, &iv);
            if (rc != OK) return rc;

            ir_vreg_t fv = ir_new_vreg(l->f, IR_TYPE_F64);
            rc = ir_emit(l->f, (ir_instr_t){
                .op  = IR_OP_I64_TO_F64,
                .dst = fv,
                .a   = iv,
                .pos = e->pos,
            });
            if (rc != OK) return rc;

            *out = fv;
            return OK;
        }
    }
}

static err_t nasm_lower_unary_i64_(nasm_lower_t* l, const ast_node_t* e, ir_vreg_t* out)
{
    ir_vreg_t a = { 0 };

    err_t rc = nasm_lower_expr_i64_(l, e->left, &a);
    if (rc != OK)
        return rc;

    if (e->u.unary.op == TOK_OP_PLUS)
    {
        *out = a;
        return OK;
    }

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
    ir_op_t op = IR_OP_NOP;

    switch (e->u.unary.op)
    {
        case TOK_OP_MINUS: op = IR_OP_NEG_I64; break;
        case TOK_OP_NOT:   op = IR_OP_NOT_I64; break;
        default:
            BE_FAIL_NODE(l->be, e, "Unsupported unary operator");
    }

    rc = ir_emit(l->f, (ir_instr_t){
        .op = op,
        .dst = dst,
        .a = a,
        .pos = e->pos,
    });

    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}

static ir_op_t binop_to_ir_(token_kind_t op)
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

static err_t nasm_lower_i64_pow_small_const_(nasm_lower_t*    l,
                                             const ast_node_t* e,
                                             ir_vreg_t*       out,
                                             const ast_node_t* base,
                                             i64_t            exp)
{
    if (exp < 0 || exp > 4)
        return ERR_BAD_ARG;

    if (exp == 0)
    {
        ir_vreg_t one = ir_new_vreg(l->f, IR_TYPE_I64);

        err_t rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_MOV_IMM_I64,
            .dst = one,
            .imm = 1,
            .pos = e->pos,
        });
        if (rc != OK)
            return rc;

        *out = one;
        return OK;
    }

    ir_vreg_t x = { 0 };
    err_t rc = nasm_lower_expr_i64_(l, base, &x);
    if (rc != OK)
        return rc;

    if (exp == 1)
    {
        *out = x;
        return OK;
    }

    ir_vreg_t cur = x;

    for (i64_t i = 1; i < exp; ++i)
    {
        ir_vreg_t next = ir_new_vreg(l->f, IR_TYPE_I64);

        rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_MUL_I64,
            .dst = next,
            .a   = cur,
            .b   = x,
            .pos = e->pos,
        });
        if (rc != OK)
            return rc;

        cur = next;
    }

    *out = cur;
    return OK;
}

static err_t nasm_lower_expr_f64_to_i64_(nasm_lower_t* l,
                                         const ast_node_t* e,
                                         ir_vreg_t* out)
{
    ir_vreg_t fv = { 0 };

    err_t rc = nasm_lower_expr_f64_(l, e, &fv);
    if (rc != OK)
        return rc;

    ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_F64_TO_I64,
        .dst = iv,
        .a   = fv,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    *out = iv;
    return OK;
}

static err_t nasm_lower_logical_and_i64_(nasm_lower_t* l,
                                         const ast_node_t* e,
                                         ir_vreg_t* out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad && node");

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    size_t L_false = ir_new_label(l->f);
    size_t L_end   = ir_new_label(l->f);

    ir_vreg_t a = { 0 };
    err_t rc = nasm_lower_expr_i64_(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JZ,
        .a        = a,
        .label_id = L_false,
        .pos      = lhs->pos,
    });
    if (rc != OK)
        return rc;

    ir_vreg_t b = { 0 };
    rc = nasm_lower_expr_i64_(l, rhs, &b);
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JZ,
        .a        = b,
        .label_id = L_false,
        .pos      = rhs->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = dst,
        .imm = 1,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JMP,
        .label_id = L_end,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_false,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = dst,
        .imm = 0,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_end,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}

static err_t nasm_lower_logical_or_i64_(nasm_lower_t* l,
                                        const ast_node_t* e,
                                        ir_vreg_t* out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad || node");

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    size_t L_rhs   = ir_new_label(l->f);
    size_t L_false = ir_new_label(l->f);
    size_t L_end   = ir_new_label(l->f);

    ir_vreg_t a = { 0 };
    err_t rc = nasm_lower_expr_i64_(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JZ,
        .a        = a,
        .label_id = L_rhs,
        .pos      = lhs->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = dst,
        .imm = 1,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JMP,
        .label_id = L_end,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_rhs,
        .pos      = rhs->pos,
    });
    if (rc != OK)
        return rc;

    ir_vreg_t b = { 0 };
    rc = nasm_lower_expr_i64_(l, rhs, &b);
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JZ,
        .a        = b,
        .label_id = L_false,
        .pos      = rhs->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = dst,
        .imm = 1,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JMP,
        .label_id = L_end,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_false,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = dst,
        .imm = 0,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    rc = ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_end,
        .pos      = e->pos,
    });
    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}

static err_t nasm_lower_float_cmp_i64_(nasm_lower_t*    l,
                                       const ast_node_t* e,
                                       ir_vreg_t*       out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad float comparison node");

    ir_op_t op = binop_to_ir_f64_(e->u.binary.op);

    BE_CHECK(l->be, op != IR_OP_NOP, e,
             "Unsupported float comparison operator");

    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };

    err_t rc = nasm_lower_expr_f64_(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = nasm_lower_expr_f64_(l, rhs, &b);
    if (rc != OK)
        return rc;

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = op,
        .dst = dst,
        .a   = a,
        .b   = b,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}

static err_t nasm_lower_binary_i64_(nasm_lower_t* l,
                                    const ast_node_t* e,
                                    ir_vreg_t* out)
{
    const ast_node_t* lhs = e->left;
    const ast_node_t* rhs = lhs ? lhs->right : NULL;

    BE_CHECK(l->be, lhs && rhs, e, "Bad binary node");

    if (e->u.binary.op == TOK_OP_AND)
        return nasm_lower_logical_and_i64_(l, e, out);

    if (e->u.binary.op == TOK_OP_OR)
        return nasm_lower_logical_or_i64_(l, e, out);

    if (e->u.binary.op == TOK_OP_POW)
{
    i64_t exp = 0;

    if (ast_i64_literal_(rhs, &exp) && exp >= 0 && exp <= 4)
        return nasm_lower_i64_pow_small_const_(l, e, out, lhs, exp);
}

    ast_type_t lt = nasm_infer_expr_type_(l->be, lhs);
    ast_type_t rt = nasm_infer_expr_type_(l->be, rhs);

    /*
        If this binary expression involves floats but is requested in i64
        context, lower it as float expression first, then cast result to i64.

        This fixes:
            dist2 < r2
            dx > dy

        because float comparisons produce 0/1, not truncated-int comparisons.
    */
   if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT)
{
    if (nasm_is_bool_op_(e->u.binary.op))
        return nasm_lower_float_cmp_i64_(l, e, out);

    return nasm_lower_expr_f64_to_i64_(l, e, out);
}

    ir_op_t op = binop_to_ir_(e->u.binary.op);

    BE_CHECK(l->be, op != IR_OP_NOP, e,
             "Integer binary operator not supported yet");

    ir_vreg_t a = { 0 };
    ir_vreg_t b = { 0 };

    err_t rc = nasm_lower_expr_i64_(l, lhs, &a);
    if (rc != OK)
        return rc;

    rc = nasm_lower_expr_i64_(l, rhs, &b);
    if (rc != OK)
        return rc;

    ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);

    rc = ir_emit(l->f, (ir_instr_t){
        .op  = op,
        .dst = dst,
        .a   = a,
        .b   = b,
        .pos = e->pos,
    });
    if (rc != OK)
        return rc;

    *out = dst;
    return OK;
}
static size_t ast_arg_count_(const ast_node_t* args)
{
    size_t n = 0;

    if (!args)
        return 0;

    for (const ast_node_t* a = args->left; a; a = a->right)
        n++;

    return n;
}

static int nasm_is_builtin_name_(const char* name)
{
    return be_streq(name, "in")       || be_streq(name, "fin")      || be_streq(name, "cin")    ||
            be_streq(name, "draw")     || be_streq(name, "clean_vm")                             ||
            be_streq(name, "out")      || be_streq(name, "fout")     || be_streq(name, "cout")   ||
            be_streq(name, "set_pixel") || be_streq(name, "sqrt")    ||
            be_streq(name, "pow")      || be_streq(name, "mpow")    ||  be_streq(name, "cap")    ||
            be_streq(name, "nocap")    || be_streq(name, "stinky") ||
            be_streq(name, "gyat")     || be_streq(name, "skibidi")                              ||
            be_streq(name, "pookie")   || be_streq(name, "rizz")     || be_streq(name, "menace");
}

static const ast_node_t* arg_at_(const ast_node_t* args, size_t idx)
{
    if (!args)
        return NULL;

    const ast_node_t* a = args->left;
    while (a && idx--)
        a = a->right;

    return a;
}

static err_t emit_zero_i64_(nasm_lower_t* l, token_pos_t pos, ir_vreg_t* out)
{
    ir_vreg_t z = ir_new_vreg(l->f, IR_TYPE_I64);

    err_t rc = ir_emit(l->f, (ir_instr_t){
        .op  = IR_OP_MOV_IMM_I64,
        .dst = z,
        .imm = 0,
        .pos = pos,
    });
    if (rc != OK) return rc;

    *out = z;
    return OK;
}

static err_t nasm_lower_call_expr_i64_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);

    if (nasm_is_builtin_name_(name))
        return nasm_lower_builtin_i64_(l, call, out);

    return nasm_lower_user_call_(l, call, out);
}

static err_t nasm_lower_call_expr_f64_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);

    if (nasm_is_builtin_name_(name))
        return nasm_lower_builtin_f64_(l, call, out);

    return nasm_lower_user_call_(l, call, out);
}

static err_t nasm_lower_builtin_i64_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    const ast_node_t* args = call->left;
    size_t argc = ast_arg_count_(args);

    if (be_streq(name, "in") || be_streq(name, "cap"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
        err_t rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_SCANF_I64, .dst = dst, .pos = call->pos });
        if (rc != OK) return rc;

        *out = dst;
        return OK;
    }

    if (be_streq(name, "cin") || be_streq(name, "stinky"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        ir_vreg_t dst = ir_new_vreg(l->f, IR_TYPE_I64);
        err_t rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_GETCHAR_I64, .dst = dst, .pos = call->pos });
        if (rc != OK) return rc;

        *out = dst;
        return OK;
    }

    if (be_streq(name, "out") || be_streq(name, "pookie"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        ir_vreg_t v = { 0 };
        err_t rc = nasm_lower_expr_i64_(l, arg_at_(args, 0), &v);
        if (rc != OK) return rc;

        rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_PRINTF_I64, .a = v, .pos = call->pos });
        if (rc != OK) return rc;

        *out = v;
        return OK;
    }

    if (be_streq(name, "cout") || be_streq(name, "menace"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        ir_vreg_t v = { 0 };
        err_t rc = nasm_lower_expr_i64_(l, arg_at_(args, 0), &v);
        if (rc != OK) return rc;

        rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_PUTCHAR_I64, .a = v, .pos = call->pos });
        if (rc != OK) return rc;

        *out = v;
        return OK;
    }

#ifndef __NASM_SIM_GRAPHICS
#define NASM_REQUIRE_GRAPHICS(call_node) \
    BE_FAIL_NODE(l->be, (call_node), "graphics builtin requires build flag NASM_GRAPHICS=1 / --graphics")
#else
#define NASM_REQUIRE_GRAPHICS(call_node) ((void)0)
#endif

    if (be_streq(name, "draw") || be_streq(name, "gyat"))
    {
        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        err_t rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_RUNTIME_DRAW, .pos = call->pos });
        if (rc != OK) return rc;

        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "clean_vm") || be_streq(name, "skibidi"))
    {
        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        err_t rc = ir_emit(l->f, (ir_instr_t){ .op = IR_OP_RUNTIME_CLEAN, .pos = call->pos });
        if (rc != OK) return rc;

        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "set_pixel"))
    {
        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 3, call, "set_pixel(x,y,ch) takes 3 args");

        ir_instr_t in = {
            .op        = IR_OP_RUNTIME_SET_PIXEL,
            .arg_count = 3,
            .pos       = call->pos,
        };

        for (size_t i = 0; i < 3; ++i)
        {
            err_t rc = nasm_lower_expr_i64_(l, arg_at_(args, i), &in.args[i]);
            if (rc != OK) return rc;
        }

        err_t rc = ir_emit(l->f, in);
        if (rc != OK) return rc;

        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "fin") || be_streq(name, "nocap") ||
        be_streq(name, "fout") || be_streq(name, "rizz"))
    {
        ir_vreg_t fv = { 0 };
        err_t rc = nasm_lower_builtin_f64_(l, call, &fv);
        if (rc != OK) return rc;

        ir_vreg_t iv = ir_new_vreg(l->f, IR_TYPE_I64);
        rc = ir_emit(l->f, (ir_instr_t){
            .op  = IR_OP_F64_TO_I64,
            .dst = iv,
            .a   = fv,
            .pos = call->pos,
        });
        if (rc != OK) return rc;

        *out = iv;
        return OK;
    }

    BE_FAIL_NODE(l->be, call, "Unknown builtin '%s'", name ? name : "<?>");
}

static ir_type_t ir_type_from_ast_(ast_type_t t)
{
    switch (t)
    {
        case AST_TYPE_FLOAT: return IR_TYPE_F64;
        case AST_TYPE_PTR:   return IR_TYPE_PTR;
        case AST_TYPE_INT:   return IR_TYPE_I64;
        default:             return IR_TYPE_VOID;
    }
}

static err_t nasm_lower_user_call_(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const func_meta_t* fm = be_find_func(l->be, call->u.call.name_id);
    BE_CHECK(l->be, fm != NULL, call, "Unknown function call");

    const ast_node_t* args = call->left;
    size_t argc = ast_arg_count_(args);

    BE_CHECK(l->be, argc == fm->param_count,
             call, "Call arg count mismatch");

    BE_CHECK(l->be, argc <= IR_MAX_CALL_ARGS,
         call, "IR supports at most %d total args", IR_MAX_CALL_ARGS);

    ir_instr_t in = {
        .op        = IR_OP_CALL,
        .dst       = { .id = IR_NO_VREG, .type = IR_TYPE_VOID },
        .func_id   = call->u.call.name_id,
        .arg_count = argc,
        .pos       = call->pos,
    };

    if (fm->ret_type != AST_TYPE_VOID)
        in.dst = ir_new_vreg(l->f, ir_type_from_ast_(fm->ret_type));

    size_t i = 0;
    for (const ast_node_t* a = args ? args->left : NULL; a; a = a->right, ++i)
    {
        err_t rc = OK;

        if (fm->param_types[i] == AST_TYPE_FLOAT)
            rc = nasm_lower_expr_f64_(l, a, &in.args[i]);
        else
            rc = nasm_lower_expr_i64_(l, a, &in.args[i]);

        if (rc != OK)
            return rc;
    }

    err_t rc = ir_emit(l->f, in);
    if (rc != OK)
        return rc;

    if (out)
        *out = in.dst;

    return OK;
}

static err_t nasm_lower_call_stmt_(nasm_lower_t* l, const ast_node_t* st)
{
    const ast_node_t* call = st->left;

    BE_CHECK(l->be, call && call->kind == ASTK_CALL,
             st, "CALL_STMT without CALL child");

    ast_type_t ty = nasm_infer_expr_type_(l->be, call);
    ir_vreg_t ignored = { 0 };

    if (ty == AST_TYPE_FLOAT)
        return nasm_lower_expr_f64_(l, call, &ignored);

    return nasm_lower_expr_i64_(l, call, &ignored);
}

static err_t nasm_lower_expr_stmt_(nasm_lower_t* l, const ast_node_t* st)
{
    ast_type_t ty = nasm_infer_expr_type_(l->be, st->left);
    ir_vreg_t ignored = { 0 };

    if (ty == AST_TYPE_FLOAT)
        return nasm_lower_expr_f64_(l, st->left, &ignored);

    return nasm_lower_expr_i64_(l, st->left, &ignored);
}
static err_t lower_loop_push_(nasm_lower_t* l, size_t label)
{
    BE_VEC_GROW(l->loop_end_labels, l->loop_cap, l->loop_amount + 1, size_t);
    l->loop_end_labels[l->loop_amount++] = label;
    return OK;
}

static void lower_loop_pop_(nasm_lower_t* l)
{
    if (l && l->loop_amount > 0)
        l->loop_amount--;
}
static int nasm_is_cmp_op_(token_kind_t op)
{
    return op == TOK_OP_EQ  ||
           op == TOK_OP_NEQ ||
           op == TOK_OP_LT  ||
           op == TOK_OP_GT  ||
           op == TOK_OP_LTE ||
           op == TOK_OP_GTE;
}

static err_t nasm_lower_cond_false_(nasm_lower_t*    l,
                                    const ast_node_t* cond,
                                    size_t            false_label)
{
    if (cond &&
        cond->kind == ASTK_BINARY &&
        cond->u.binary.op == TOK_OP_AND)
    {
        const ast_node_t* lhs = cond->left;
        const ast_node_t* rhs = lhs ? lhs->right : NULL;

        BE_CHECK(l->be, lhs && rhs, cond, "Bad && condition");

        err_t rc = nasm_lower_cond_false_(l, lhs, false_label);
        if (rc != OK)
            return rc;

        return nasm_lower_cond_false_(l, rhs, false_label);
    }

    if (cond &&
        cond->kind == ASTK_BINARY &&
        nasm_is_cmp_op_(cond->u.binary.op))
    {
        const ast_node_t* lhs = cond->left;
        const ast_node_t* rhs = lhs ? lhs->right : NULL;

        BE_CHECK(l->be, lhs && rhs, cond, "Bad comparison condition");

        ast_type_t lt = nasm_infer_expr_type_(l->be, lhs);
        ast_type_t rt = nasm_infer_expr_type_(l->be, rhs);

        ir_vreg_t a = { 0 };
        ir_vreg_t b = { 0 };

        err_t rc = OK;

        if (lt == AST_TYPE_FLOAT || rt == AST_TYPE_FLOAT)
        {
            rc = nasm_lower_expr_f64_(l, lhs, &a);
            if (rc != OK) return rc;

            rc = nasm_lower_expr_f64_(l, rhs, &b);
            if (rc != OK) return rc;

            return ir_emit(l->f, (ir_instr_t){
                .op       = IR_OP_JCC_FALSE_F64,
                .a        = a,
                .b        = b,
                .imm      = cond->u.binary.op,
                .label_id = false_label,
                .pos      = cond->pos,
            });
        }

        rc = nasm_lower_expr_i64_(l, lhs, &a);
        if (rc != OK) return rc;

        rc = nasm_lower_expr_i64_(l, rhs, &b);
        if (rc != OK) return rc;

        return ir_emit(l->f, (ir_instr_t){
            .op       = IR_OP_JCC_FALSE_I64,
            .a        = a,
            .b        = b,
            .imm      = cond->u.binary.op,
            .label_id = false_label,
            .pos      = cond->pos,
        });
    }

    ir_vreg_t c = { 0 };
    err_t rc = nasm_lower_expr_i64_(l, cond, &c);
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_JZ,
        .a        = c,
        .label_id = false_label,
        .pos      = cond->pos,
    });
}
static err_t nasm_lower_jz_(nasm_lower_t* l,
                            const ast_node_t* cond,
                            size_t false_label)
{
    return nasm_lower_cond_false_(l, cond, false_label);
}
static err_t nasm_lower_while_(nasm_lower_t* l, const ast_node_t* w)
{
    const ast_node_t* cond = w->left;
    const ast_node_t* body = cond ? cond->right : NULL;

    BE_CHECK(l->be, cond && body, w, "Bad WHILE node");

    size_t L_begin = ir_new_label(l->f);
    size_t L_end   = ir_new_label(l->f);

    err_t rc = ir_emit(l->f, (ir_instr_t){
        .op = IR_OP_LABEL,
        .label_id = L_begin,
        .pos = w->pos,
    });
    if (rc != OK)
        return rc;

    rc = nasm_lower_jz_(l, cond, L_end);
    if (rc != OK)
        return rc;

    rc = lower_loop_push_(l, L_end);
    if (rc != OK)
        return rc;

    rc = nasm_lower_stmt_(l, body);
    if (rc != OK)
        return rc;

    lower_loop_pop_(l);

    rc = ir_emit(l->f, (ir_instr_t){
        .op = IR_OP_JMP,
        .label_id = L_begin,
        .pos = w->pos,
    });
    if (rc != OK)
        return rc;

    return ir_emit(l->f, (ir_instr_t){
        .op = IR_OP_LABEL,
        .label_id = L_end,
        .pos = w->pos,
    });
}

static err_t nasm_lower_break_(nasm_lower_t* l, const ast_node_t* brk)
{
    BE_CHECK(l->be, l->loop_amount > 0, brk, "break outside loop");

    size_t L_end = l->loop_end_labels[l->loop_amount - 1];

    return ir_emit(l->f, (ir_instr_t){
        .op = IR_OP_JMP,
        .label_id = L_end,
        .pos = brk->pos,
    });
}

static err_t nasm_lower_if_(nasm_lower_t* l, const ast_node_t* ifn)
{
    const ast_node_t* cond = ifn->left;
    const ast_node_t* then_st = cond ? cond->right : NULL;
    const ast_node_t* tail = then_st ? then_st->right : NULL;

    BE_CHECK(l->be, cond && then_st, ifn, "Bad IF node");

    size_t L_end = ir_new_label(l->f);
    size_t L_next = tail ? ir_new_label(l->f) : L_end;

    err_t rc = nasm_lower_jz_(l, cond, L_next);
    if (rc != OK)
        return rc;

    rc = nasm_lower_stmt_(l, then_st);
    if (rc != OK)
        return rc;

    if (tail)
    {
        rc = ir_emit(l->f, (ir_instr_t){
            .op       = IR_OP_JMP,
            .label_id = L_end,
            .pos      = ifn->pos,
        });
        if (rc != OK)
            return rc;

        rc = ir_emit(l->f, (ir_instr_t){
            .op       = IR_OP_LABEL,
            .label_id = L_next,
            .pos      = tail->pos,
        });
        if (rc != OK)
            return rc;

        rc = nasm_lower_if_tail_(l, tail, L_end);
        if (rc != OK)
            return rc;
    }

    return ir_emit(l->f, (ir_instr_t){
        .op       = IR_OP_LABEL,
        .label_id = L_end,
        .pos      = ifn->pos,
    });
}

static err_t nasm_lower_if_tail_(nasm_lower_t* l, const ast_node_t* tail, size_t L_end)
{
    if (!tail)
        return OK;

    if (tail->kind == ASTK_ELSE)
    {
        const ast_node_t* body = tail->left;

        BE_CHECK(l->be, body != NULL, tail, "Bad ELSE node");

        return nasm_lower_stmt_(l, body);
    }

    if (tail->kind == ASTK_BRANCH)
    {
        const ast_node_t* cond = tail->left;
        const ast_node_t* body = cond ? cond->right : NULL;
        const ast_node_t* next = body ? body->right : NULL;

        BE_CHECK(l->be, cond && body, tail, "Bad ELIF branch");

        size_t L_next = next ? ir_new_label(l->f) : L_end;

        err_t rc = nasm_lower_jz_(l, cond, L_next);
        if (rc != OK)
            return rc;

        rc = nasm_lower_stmt_(l, body);
        if (rc != OK)
            return rc;

        if (next)
        {
            rc = ir_emit(l->f, (ir_instr_t){
                .op       = IR_OP_JMP,
                .label_id = L_end,
                .pos      = tail->pos,
            });
            if (rc != OK)
                return rc;

            rc = ir_emit(l->f, (ir_instr_t){
                .op       = IR_OP_LABEL,
                .label_id = L_next,
                .pos      = next->pos,
            });
            if (rc != OK)
                return rc;

            return nasm_lower_if_tail_(l, next, L_end);
        }

        return OK;
    }

    BE_FAIL_NODE(l->be, tail, "Bad IF tail child %s", ast_kind_to_cstr(tail->kind));
}

/*
    Nasm emiting
*/
err_t backend_emit_nasm(const ast_tree_t* tree, operational_data_t* op_data)
{
    if (!tree || !tree->root || !op_data)
        return ERR_BAD_ARG;

    op_data->error_pos    = 0;
    op_data->error_msg[0] = '\0';

    backend_t be = { 0 };
    be.tree      = tree;
    be.op        = op_data;

    const ast_node_t* program = tree->root;

    if (program->kind != ASTK_PROGRAM)
    {
        be_set_error(&be, program->pos, program->pos.offset,
                     "Root is not PROGRAM");
        return ERR_SYNTAX;
    }

    err_t rc = be_collect_funcs(&be, program, "__fn_%s");
    if (rc != OK)
        goto cleanup;

    size_t main_id = be_find_name(tree, "main");
    if (main_id == SIZE_MAX || !be_find_func(&be, main_id))
    {
        be_set_error(&be, (token_pos_t){1, 1, 0}, 0,
                     "No function 'main' found");
        rc = ERR_SYNTAX;
        goto cleanup;
    }

    rc = nasm_emit_program_(&be, program);

cleanup:
    be_free(&be);
    return rc;
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
    be_emitf(be, "__brl_unget_ch:      resb 1\n");
    be_emitf(be, "__brl_has_unget:     resb 1\n");

#ifdef __NASM_SIM_GRAPHICS
    be_emitf(be, "__brl_screen:       resb %d\n", BE_SCREEN_BYTES);
#endif

    be_emitf(be, "\nsection .text\n\n");
}
static err_t nasm_emit_c_main_(backend_t* be, const ast_node_t* program)
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

static void nasm_emit_runtime_(backend_t* be, const nasm_runtime_use_t* rt)
{
    if (!rt)
        return;

    if (rt->ipow_i64)
    {
        be_emitf(be, "__brl_ipow_i64:\n");
        be_emitf(be, "    mov  rax, 1\n");
        be_emitf(be, "    test rsi, rsi\n");
        be_emitf(be, "    js   .L_ipow_done\n");
        be_emitf(be, ".L_ipow_loop:\n");
        be_emitf(be, "    test rsi, rsi\n");
        be_emitf(be, "    jz   .L_ipow_done\n");
        be_emitf(be, "    test rsi, 1\n");
        be_emitf(be, "    jz   .L_ipow_skip_mul\n");
        be_emitf(be, "    imul rax, rdi\n");
        be_emitf(be, ".L_ipow_skip_mul:\n");
        be_emitf(be, "    imul rdi, rdi\n");
        be_emitf(be, "    sar  rsi, 1\n");
        be_emitf(be, "    jmp  .L_ipow_loop\n");
        be_emitf(be, ".L_ipow_done:\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->putchar_)
    {
        be_emitf(be, "__brl_putchar:\n");
        be_emitf(be, "    mov  [__brl_ch], dil\n");
        be_emitf(be, "    mov  rax, 1\n");
        be_emitf(be, "    mov  rdi, 1\n");
        be_emitf(be, "    lea  rsi, [__brl_ch]\n");
        be_emitf(be, "    mov  rdx, 1\n");
        be_emitf(be, "    syscall\n");
        be_emitf(be, "    xor  eax, eax\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->getchar_)
    {
        be_emitf(be,
        "__brl_getchar:\n"
        "    cmp  byte [__brl_has_unget], 0\n"
        "    je   .L_getchar_read\n"
        "    mov  byte [__brl_has_unget], 0\n"
        "    movzx rax, byte [__brl_unget_ch]\n"
        "    ret\n"
        ".L_getchar_read:\n"
        "    mov  rax, 0\n"
        "    mov  rdi, 0\n"
        "    lea  rsi, [__brl_ch]\n"
        "    mov  rdx, 1\n"
        "    syscall\n"
        "    cmp  rax, 1\n"
        "    jne  .L_getchar_eof\n"
        "    movzx rax, byte [__brl_ch]\n"
        "    ret\n"
        ".L_getchar_eof:\n"
        "    mov  rax, -1\n"
        "    ret\n\n");
    }

    if (rt->ungetchar_)
    {
        be_emitf(be,
        "__brl_ungetchar:\n"
        "    cmp  rdi, 0\n"
        "    jl   .L_unget_ret\n"
        "    mov  [__brl_unget_ch], dil\n"
        "    mov  byte [__brl_has_unget], 1\n"
        ".L_unget_ret:\n"
        "    ret\n\n");
    }

    if (rt->print_i64)
    {
        be_emitf(be, "__brl_print_i64:\n");
        be_emitf(be, "    push rbx\n");
        be_emitf(be, "    push r12\n");
        be_emitf(be, "    lea  rsi, [__brl_outbuf + 127]\n");
        be_emitf(be, "    mov  byte [rsi], 10\n");
        be_emitf(be, "    mov  rax, rdi\n");
        be_emitf(be, "    xor  r12d, r12d\n");
        be_emitf(be, "    cmp  rax, 0\n");
        be_emitf(be, "    jge  .L_pi_abs\n");
        be_emitf(be, "    mov  r12d, 1\n");
        be_emitf(be, "    neg  rax\n");
        be_emitf(be, ".L_pi_abs:\n");
        be_emitf(be, "    mov  rbx, 10\n");
        be_emitf(be, "    cmp  rax, 0\n");
        be_emitf(be, "    jne  .L_pi_loop\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  byte [rsi], '0'\n");
        be_emitf(be, "    jmp  .L_pi_sign\n");
        be_emitf(be, ".L_pi_loop:\n");
        be_emitf(be, "    xor  rdx, rdx\n");
        be_emitf(be, "    div  rbx\n");
        be_emitf(be, "    add  dl, '0'\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  [rsi], dl\n");
        be_emitf(be, "    test rax, rax\n");
        be_emitf(be, "    jne  .L_pi_loop\n");
        be_emitf(be, ".L_pi_sign:\n");
        be_emitf(be, "    test r12d, r12d\n");
        be_emitf(be, "    jz   .L_pi_write\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  byte [rsi], '-'\n");
        be_emitf(be, ".L_pi_write:\n");
        be_emitf(be, "    lea  rdx, [__brl_outbuf + 128]\n");
        be_emitf(be, "    sub  rdx, rsi\n");
        be_emitf(be, "    mov  rax, 1\n");
        be_emitf(be, "    mov  rdi, 1\n");
        be_emitf(be, "    syscall\n");
        be_emitf(be, "    pop  r12\n");
        be_emitf(be, "    pop  rbx\n");
        be_emitf(be, "    xor  eax, eax\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->scan_i64)
    {
        be_emitf(be,
        "__brl_scan_i64:\n"
        "    push rbx\n"
        "    xor  ebx, ebx              ; result\n"
        "    xor  r8d, r8d              ; negative flag\n"
        "\n"
        ".L_si_skip_ws:\n"
        "    call __brl_getchar\n"
        "    cmp  rax, 0\n"
        "    jl   .L_si_zero\n"
        "    cmp  rax, ' '\n"
        "    jle  .L_si_skip_ws\n"
        "    mov  r9, rax               ; current char\n"
        "\n"
        "    cmp  r9, '-'\n"
        "    jne  .L_si_check_plus\n"
        "    mov  r8d, 1\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "    jmp  .L_si_loop\n"
        "\n"
        ".L_si_check_plus:\n"
        "    cmp  r9, '+'\n"
        "    jne  .L_si_loop\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "\n"
        ".L_si_loop:\n"
        "    cmp  r9, '0'\n"
        "    jb   .L_si_done\n"
        "    cmp  r9, '9'\n"
        "    ja   .L_si_done\n"
        "    imul rbx, rbx, 10\n"
        "    mov  r10, r9\n"
        "    sub  r10, '0'\n"
        "    add  rbx, r10\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "    jmp  .L_si_loop\n"
        "\n"
        ".L_si_done:\n"
        "    mov  rdi, r9\n"
        "    call __brl_ungetchar\n"
        "    mov  rax, rbx\n"
        "    test r8d, r8d\n"
        "    jz   .L_si_ret\n"
        "    neg  rax\n"
        "    jmp  .L_si_ret\n"
        "\n"
        ".L_si_zero:\n"
        "    xor  eax, eax\n"
        "\n"
        ".L_si_ret:\n"
        "    pop  rbx\n"
        "    ret\n\n");
    }

    if (rt->scan_f64)
    {
        be_emitf(be,
        "__brl_scan_f64:\n"
        "    ; returns parsed double in xmm0\n"
        "    ; grammar: [spaces] ['+'|'-'] digits? ['.' digits?]\n"
        "    movsd xmm0, [__f64_zero]\n"
        "    xor  r8d, r8d              ; r8d = negative flag\n"
        "\n"
        ".L_sf_skip_ws:\n"
        "    call __brl_getchar\n"
        "    cmp  rax, 0\n"
        "    jl   .L_sf_ret_zero\n"
        "    cmp  rax, ' '\n"
        "    jle  .L_sf_skip_ws\n"
        "    mov  r9, rax               ; r9 = current char\n"
        "\n"
        "    cmp  r9, '-'\n"
        "    jne  .L_sf_check_plus\n"
        "    mov  r8d, 1\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "    jmp  .L_sf_int_loop\n"
        "\n"
        ".L_sf_check_plus:\n"
        "    cmp  r9, '+'\n"
        "    jne  .L_sf_int_loop\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "\n"
        ".L_sf_int_loop:\n"
        "    cmp  r9, '0'\n"
        "    jb   .L_sf_check_dot\n"
        "    cmp  r9, '9'\n"
        "    ja   .L_sf_check_dot\n"
        "\n"
        "    mulsd xmm0, [__f64_ten]\n"
        "    mov  r10, r9\n"
        "    sub  r10, '0'\n"
        "    cvtsi2sd xmm1, r10\n"
        "    addsd xmm0, xmm1\n"
        "\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "    jmp  .L_sf_int_loop\n"
        "\n"
        ".L_sf_check_dot:\n"
        "    cmp  r9, '.'\n"
        "    jne  .L_sf_apply_sign\n"
        "\n"
        "    movsd xmm2, [__f64_ten]     ; denominator = 10.0\n"
        "\n"
        ".L_sf_frac_next:\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "\n"
        ".L_sf_frac_loop:\n"
        "    cmp  r9, '0'\n"
        "    jb   .L_sf_apply_sign\n"
        "    cmp  r9, '9'\n"
        "    ja   .L_sf_apply_sign\n"
        "\n"
        "    mov  r10, r9\n"
        "    sub  r10, '0'\n"
        "    cvtsi2sd xmm1, r10\n"
        "    divsd xmm1, xmm2\n"
        "    addsd xmm0, xmm1\n"
        "    mulsd xmm2, [__f64_ten]\n"
        "\n"
        "    call __brl_getchar\n"
        "    mov  r9, rax\n"
        "    jmp  .L_sf_frac_loop\n"
        "\n"
        ".L_sf_apply_sign:\n"
        "    mov  rdi, r9\n"
        "    call __brl_ungetchar\n"
        "    test r8d, r8d\n"
        "    jz   .L_sf_ret\n"
        "    xorpd xmm0, [__f64_sign_mask]\n"
        "    ret\n"
        ".L_sf_ret_zero:\n"
        "    movsd xmm0, [__f64_zero]\n"
        "    ret\n"
        "\n"
        ".L_sf_ret:\n"
        "    ret\n\n");
    }

    if (rt->print_f64)
    {
        be_emitf(be,
        "__brl_print_f64:\n"
        "    ; fixed 6 digits after dot\n"
        "    push rbx\n"
        "\n"
        "    movq rax, xmm0\n"
        "    test rax, rax\n"
        "    jns  .L_pf_pos\n"
        "    mov  dil, '-'\n"
        "    call __brl_putchar\n"
        "    xorpd xmm0, [__f64_sign_mask]\n"
        "\n"
        ".L_pf_pos:\n"
        "    cvttsd2si rdi, xmm0\n"
        "    cvtsi2sd xmm1, rdi\n"
        "    subsd xmm0, xmm1\n"
        "    movsd [__brl_tmp_f], xmm0\n"
        "    call __brl_print_i64_no_nl\n"
        "\n"
        "    mov  dil, '.'\n"
        "    call __brl_putchar\n"
        "\n"
        "    movsd xmm0, [__brl_tmp_f]\n"
        "    mov  rbx, 6\n"
        "\n"
        ".L_pf_frac:\n"
        "    mulsd xmm0, [__f64_ten]\n"
        "    cvttsd2si r10, xmm0       ; digit = int(frac * 10)\n"
        "    mov  rdi, r10\n"
        "    add  dil, '0'\n"
        "    call __brl_putchar\n"
        "\n"
        "    cvtsi2sd xmm1, r10\n"
        "    subsd xmm0, xmm1\n"
        "    dec  rbx\n"
        "    jne  .L_pf_frac\n"
        "\n"
        "    mov  dil, 10\n"
        "    call __brl_putchar\n"
        "    pop  rbx\n"
        "    xor  eax, eax\n"
        "    ret\n\n");
    }

    if (rt->floor_f64)
    {
        be_emitf(be, "__brl_floor_f64:\n");
        be_emitf(be, "    cvttsd2si rax, xmm0\n");
        be_emitf(be, "    cvtsi2sd xmm1, rax\n");
        be_emitf(be, "    ucomisd xmm1, xmm0\n");
        be_emitf(be, "    jbe  .L_floor_ret\n");
        be_emitf(be, "    sub  rax, 1\n");
        be_emitf(be, ".L_floor_ret:\n");
        be_emitf(be, "    cvtsi2sd xmm0, rax\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->ceil_f64)
    {
        be_emitf(be, "__brl_ceil_f64:\n");
        be_emitf(be, "    cvttsd2si rax, xmm0\n");
        be_emitf(be, "    cvtsi2sd xmm1, rax\n");
        be_emitf(be, "    ucomisd xmm1, xmm0\n");
        be_emitf(be, "    jae  .L_ceil_ret\n");
        be_emitf(be, "    add  rax, 1\n");
        be_emitf(be, ".L_ceil_ret:\n");
        be_emitf(be, "    cvtsi2sd xmm0, rax\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->round_f64)
    {
        be_emitf(be, "__brl_round_f64:\n");
        be_emitf(be, "    addsd xmm0, [__f64_half]\n");
        be_emitf(be, "    jmp  __brl_floor_f64\n\n");
    }

    if (rt->sqrt_f64)
    {
        be_emitf(be, "__brl_sqrt_f64:\n");
        be_emitf(be, "    ; Newton sqrt, input/output xmm0\n");
        be_emitf(be, "    ucomisd xmm0, [__f64_zero]\n");
        be_emitf(be, "    jbe  .L_sqrt_ret\n");
        be_emitf(be, "    movapd xmm1, xmm0\n");
        be_emitf(be, "    mov  rcx, 24\n");
        be_emitf(be, ".L_sqrt_loop:\n");
        be_emitf(be, "    movapd xmm2, xmm0\n");
        be_emitf(be, "    divsd xmm2, xmm1\n");
        be_emitf(be, "    addsd xmm1, xmm2\n");
        be_emitf(be, "    mulsd xmm1, [__f64_half]\n");
        be_emitf(be, "    dec  rcx\n");
        be_emitf(be, "    jne  .L_sqrt_loop\n");
        be_emitf(be, "    movapd xmm0, xmm1\n");
        be_emitf(be, ".L_sqrt_ret:\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->fpow_f64)
    {
        be_emitf(be, "__brl_fpow_f64:\n");
        be_emitf(be, "    ; input: xmm0=base, xmm1=exponent. Supports integer exponent only.\n");
        be_emitf(be, "    cvttsd2si rdi, xmm1\n");
        be_emitf(be, "    movapd xmm2, xmm0\n");
        be_emitf(be, "    movsd xmm0, [__f64_one]\n");
        be_emitf(be, "    cmp  rdi, 0\n");
        be_emitf(be, "    jge  .L_fpow_loop\n");
        be_emitf(be, "    neg  rdi\n");
        be_emitf(be, "    mov  r8d, 1\n");
        be_emitf(be, "    jmp  .L_fpow_loop_start\n");
        be_emitf(be, ".L_fpow_loop:\n");
        be_emitf(be, "    xor  r8d, r8d\n");
        be_emitf(be, ".L_fpow_loop_start:\n");
        be_emitf(be, "    test rdi, rdi\n");
        be_emitf(be, "    jz   .L_fpow_done\n");
        be_emitf(be, "    test rdi, 1\n");
        be_emitf(be, "    jz   .L_fpow_skip\n");
        be_emitf(be, "    mulsd xmm0, xmm2\n");
        be_emitf(be, ".L_fpow_skip:\n");
        be_emitf(be, "    mulsd xmm2, xmm2\n");
        be_emitf(be, "    sar  rdi, 1\n");
        be_emitf(be, "    jmp  .L_fpow_loop_start\n");
        be_emitf(be, ".L_fpow_done:\n");
        be_emitf(be, "    test r8d, r8d\n");
        be_emitf(be, "    jz   .L_fpow_ret\n");
        be_emitf(be, "    movsd xmm1, [__f64_one]\n");
        be_emitf(be, "    divsd xmm1, xmm0\n");
        be_emitf(be, "    movapd xmm0, xmm1\n");
        be_emitf(be, ".L_fpow_ret:\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->print_i64_no_nl)
    {
        be_emitf(be, "__brl_print_i64_no_nl:\n");
        be_emitf(be, "    push rbx\n");
        be_emitf(be, "    push r12\n");
        be_emitf(be, "    lea  rsi, [__brl_outbuf + 128]\n");
        be_emitf(be, "    mov  rax, rdi\n");
        be_emitf(be, "    xor  r12d, r12d\n");
        be_emitf(be, "    cmp  rax, 0\n");
        be_emitf(be, "    jge  .L_pin_abs\n");
        be_emitf(be, "    mov  r12d, 1\n");
        be_emitf(be, "    neg  rax\n");
        be_emitf(be, ".L_pin_abs:\n");
        be_emitf(be, "    mov  rbx, 10\n");
        be_emitf(be, "    cmp  rax, 0\n");
        be_emitf(be, "    jne  .L_pin_loop\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  byte [rsi], '0'\n");
        be_emitf(be, "    jmp  .L_pin_sign\n");
        be_emitf(be, ".L_pin_loop:\n");
        be_emitf(be, "    xor  rdx, rdx\n");
        be_emitf(be, "    div  rbx\n");
        be_emitf(be, "    add  dl, '0'\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  [rsi], dl\n");
        be_emitf(be, "    test rax, rax\n");
        be_emitf(be, "    jne  .L_pin_loop\n");
        be_emitf(be, ".L_pin_sign:\n");
        be_emitf(be, "    test r12d, r12d\n");
        be_emitf(be, "    jz   .L_pin_write\n");
        be_emitf(be, "    dec  rsi\n");
        be_emitf(be, "    mov  byte [rsi], '-'\n");
        be_emitf(be, ".L_pin_write:\n");
        be_emitf(be, "    lea  rdx, [__brl_outbuf + 128]\n");
        be_emitf(be, "    sub  rdx, rsi\n");
        be_emitf(be, "    mov  rax, 1\n");
        be_emitf(be, "    mov  rdi, 1\n");
        be_emitf(be, "    syscall\n");
        be_emitf(be, "    pop  r12\n");
        be_emitf(be, "    pop  rbx\n");
        be_emitf(be, "    xor  eax, eax\n");
        be_emitf(be, "    ret\n\n");
    }

#ifdef __NASM_SIM_GRAPHICS
    if (rt->clean_vm)
    {
be_emitf(be, "__brl_clean_vm:\n");
be_emitf(be, "    lea  rdi, [__brl_screen]\n");
be_emitf(be, "    mov  r8, %d\n", BE_SCREEN_HEIGHT);
be_emitf(be, ".L_clean_row:\n");
be_emitf(be, "    mov  rcx, %d\n", BE_SCREEN_WIDTH);
be_emitf(be, "    mov  al, ' '\n");
be_emitf(be, "    rep  stosb\n");
be_emitf(be, "    mov  byte [rdi], 10\n");
be_emitf(be, "    inc  rdi\n");
be_emitf(be, "    dec  r8\n");
be_emitf(be, "    jne  .L_clean_row\n");
be_emitf(be, "    xor  eax, eax\n");
be_emitf(be, "    ret\n\n");    }

    if (rt->set_pixel)
    {
        be_emitf(be, "__brl_set_pixel:\n");
        be_emitf(be, "    cmp  rdi, 0\n");
        be_emitf(be, "    jl   .L_set_pixel_ret\n");
        be_emitf(be, "    cmp  rsi, 0\n");
        be_emitf(be, "    jl   .L_set_pixel_ret\n");
        be_emitf(be, "    cmp  rdi, %d\n", BE_SCREEN_WIDTH);
        be_emitf(be, "    jae  .L_set_pixel_ret\n");
        be_emitf(be, "    cmp  rsi, %d\n", BE_SCREEN_HEIGHT);
        be_emitf(be, "    jae  .L_set_pixel_ret\n");
        be_emitf(be, "    mov  rax, rsi\n");
be_emitf(be, "    imul rax, %d\n", BE_SCREEN_STRIDE);
be_emitf(be, "    add  rax, rdi\n");
        be_emitf(be, "    lea  rcx, [__brl_screen]\n");
        be_emitf(be, "    mov  [rcx + rax], dl\n");
        be_emitf(be, ".L_set_pixel_ret:\n");
        be_emitf(be, "    xor  eax, eax\n");
        be_emitf(be, "    ret\n\n");
    }

    if (rt->draw)
    {
be_emitf(be, "__brl_draw:\n");
be_emitf(be, "    mov  rax, 1\n");
be_emitf(be, "    mov  rdi, 1\n");
be_emitf(be, "    lea  rsi, [__brl_screen]\n");
be_emitf(be, "    mov  rdx, %d\n", BE_SCREEN_BYTES);
be_emitf(be, "    syscall\n");
be_emitf(be, "    xor  eax, eax\n");
be_emitf(be, "    ret\n\n");    }
#endif
}
static void nasm_collect_runtime_use_(nasm_runtime_use_t* rt,
                                      const ir_func_t*    f)
{
    if (!rt || !f)
        return;

    for (size_t i = 0; i < f->instr_count; ++i)
    {
        const ir_instr_t* in = &f->instrs[i];

        switch (in->op)
        {
            case IR_OP_PRINTF_I64:
                rt->print_i64 = 1;
                break;

            case IR_OP_PRINTF_F64:
                rt->print_f64 = 1;
                rt->print_i64_no_nl = 1;
                rt->putchar_ = 1;
                break;

            case IR_OP_PUTCHAR_I64:
                rt->putchar_ = 1;
                break;

            case IR_OP_SCANF_I64:
                rt->scan_i64 = 1;
                rt->getchar_ = 1;
                rt->ungetchar_ = 1;
                break;

            case IR_OP_SCANF_F64:
                rt->scan_f64 = 1;
                rt->getchar_ = 1;
                rt->ungetchar_ = 1;
                break;

            case IR_OP_GETCHAR_I64:
                rt->getchar_ = 1;
                break;

            case IR_OP_FLOOR_F64:
                rt->floor_f64 = 1;
                break;

            case IR_OP_CEIL_F64:
                rt->ceil_f64 = 1;
                break;

            case IR_OP_ROUND_F64:
                rt->round_f64 = 1;
                rt->floor_f64 = 1;
                break;

            case IR_OP_SQRT_F64:
                rt->sqrt_f64 = 1;
                break;

            case IR_OP_POW_F64:
                rt->fpow_f64 = 1;
                break;

            case IR_OP_RUNTIME_CLEAN:
                rt->clean_vm = 1;
                break;

            case IR_OP_RUNTIME_SET_PIXEL:
                rt->set_pixel = 1;
                break;

            case IR_OP_RUNTIME_DRAW:
                rt->draw = 1;

                break;

            case IR_OP_POW_I64:
    rt->ipow_i64 = 1;
    break;

            default:
                break;
        }
    }
}

static err_t nasm_emit_program_(backend_t* be, const ast_node_t* program)
{
    nasm_runtime_use_t rt = { 0 };

    nasm_emit_header_(be);
    nasm_emit_sections_(be);

    err_t rc = nasm_emit_c_main_(be, program);
    if (rc != OK)
        return rc;

    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        ir_func_t ir = { 0 };
        ir_alloc_t alloc = { 0 };

        rc = nasm_lower_func_(be, fn, &ir);
        if (rc != OK)
        {
            ir_func_dtor(&ir);
            return rc;
        }

        rc = ir_optimize_func(&ir);
        if (rc != OK)
        {
            ir_func_dtor(&ir);
            return rc;
        }

        nasm_collect_runtime_use_(&rt, &ir);

        rc = ir_alloc_run_linear_scan(&ir, &alloc);
        if (rc != OK)
        {
            ir_alloc_dtor(&alloc);
            ir_func_dtor(&ir);
            return rc;
        }

        rc = nasm_emit_ir_func_(be, &ir, &alloc);

        ir_alloc_dtor(&alloc);
        ir_func_dtor(&ir);

        if (rc != OK)
            return rc;
    }

    nasm_emit_runtime_(be, &rt);

    be_emitf(be, "section .note.GNU-stack noalloc noexec nowrite progbits\n");

    return OK;
}

static int i64_fits_i32_(i64_t x)
{
    return x >= -2147483647LL - 1LL && x <= 2147483647LL;
}

static void emit_mov_imm_i64_to_vreg_(backend_t*        be,
                                      const ir_func_t*  f,
                                      const ir_alloc_t* a,
                                      ir_vreg_t         v,
                                      i64_t             imm)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    mov  %s, %lld\n",
                 NASM_PREG64[loc.preg],
                 (long long)imm);
        return;
    }

    /*
        x86-64 cannot encode arbitrary imm64 directly into memory.
        mem, imm32 is okay. For full imm64, go through rax.
    */
    if (i64_fits_i32_(imm))
    {
        be_emitf(be, "    mov  qword ");
        emit_spill_(be, f, loc.spill_slot);
        be_emitf(be, ", %lld\n", (long long)imm);
        return;
    }

    be_emitf(be, "    mov  rax, %lld\n", (long long)imm);
    be_emitf(be, "    mov  ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

static void emit_mov_imm_f64_to_vreg_(backend_t*        be,
                                      const ir_func_t*  f,
                                      const ir_alloc_t* a,
                                      ir_vreg_t         v,
                                      i64_t             bits)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    be_emitf(be, "    mov  rax, %lld\n", (long long)bits);

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    movq %s, rax\n", NASM_XREG64[loc.preg]);
        return;
    }

    be_emitf(be, "    mov  ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

static void emit_load_slot_to_vreg_i64_(backend_t*        be,
                                        const ir_func_t*  f,
                                        const ir_alloc_t* a,
                                        ir_vreg_t         dst,
                                        size_t            slot)
{
    ir_alloc_loc_t loc = a->vreg_locs[dst.id];

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    mov  %s, ", NASM_PREG64[loc.preg]);
        emit_slot_(be, slot);
        be_emitf(be, "\n");
        return;
    }

    be_emitf(be, "    mov  rax, ");
    emit_slot_(be, slot);
    be_emitf(be, "\n");

    be_emitf(be, "    mov  ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

static void emit_store_slot_from_vreg_i64_(backend_t*        be,
                                           const ir_func_t*  f,
                                           const ir_alloc_t* a,
                                           ir_vreg_t         src,
                                           size_t            slot)
{
    ir_alloc_loc_t loc = a->vreg_locs[src.id];

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    mov  ");
        emit_slot_(be, slot);
        be_emitf(be, ", %s\n", NASM_PREG64[loc.preg]);
        return;
    }

    be_emitf(be, "    mov  rax, ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, "\n");

    be_emitf(be, "    mov  ");
    emit_slot_(be, slot);
    be_emitf(be, ", rax\n");
}

static void emit_load_slot_to_vreg_f64_(backend_t*        be,
                                        const ir_func_t*  f,
                                        const ir_alloc_t* a,
                                        ir_vreg_t         dst,
                                        size_t            slot)
{
    ir_alloc_loc_t loc = a->vreg_locs[dst.id];

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    movsd %s, ", NASM_XREG64[loc.preg]);
        emit_slot_(be, slot);
        be_emitf(be, "\n");
        return;
    }

    be_emitf(be, "    mov  rax, ");
    emit_slot_(be, slot);
    be_emitf(be, "\n");

    be_emitf(be, "    mov  ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

static void emit_store_slot_from_vreg_f64_(backend_t*        be,
                                           const ir_func_t*  f,
                                           const ir_alloc_t* a,
                                           ir_vreg_t         src,
                                           size_t            slot)
{
    ir_alloc_loc_t loc = a->vreg_locs[src.id];

    if (loc.kind == IR_LOC_REG)
    {
        be_emitf(be, "    movsd ");
        emit_slot_(be, slot);
        be_emitf(be, ", %s\n", NASM_XREG64[loc.preg]);
        return;
    }

    be_emitf(be, "    mov  rax, ");
    emit_spill_(be, f, loc.spill_slot);
    be_emitf(be, "\n");

    be_emitf(be, "    mov  ");
    emit_slot_(be, slot);
    be_emitf(be, ", rax\n");
}
static void emit_load_vreg_(backend_t*        be,
                            const ir_func_t*  f,
                            const ir_alloc_t* a,
                            ir_vreg_t         v,
                            const char*       scratch)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    if (loc.kind == IR_LOC_REG)
    {
        const char* r = NASM_PREG64[loc.preg];

        if (strcmp(r, scratch) != 0)
            be_emitf(be, "    mov  %s, %s\n", scratch, r);
    }
    else
    {
        be_emitf(be, "    mov  %s, ", scratch);
        emit_spill_(be, f, loc.spill_slot);
        be_emitf(be, "\n");
    }
}

static void emit_store_vreg_(backend_t*        be,
                             const ir_func_t*  f,
                             const ir_alloc_t* a,
                             ir_vreg_t         v,
                             const char*       scratch)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    if (loc.kind == IR_LOC_REG)
    {
        const char* r = NASM_PREG64[loc.preg];

        if (strcmp(r, scratch) != 0)
            be_emitf(be, "    mov  %s, %s\n", r, scratch);
    }
    else
    {
        be_emitf(be, "    mov  ");
        emit_spill_(be, f, loc.spill_slot);
        be_emitf(be, ", %s\n", scratch);
    }
}

static void emit_load_freg_(backend_t*        be,
                            const ir_func_t*  f,
                            const ir_alloc_t* a,
                            ir_vreg_t         v,
                            const char*       scratch)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    if (loc.kind == IR_LOC_REG)
    {
        const char* r = NASM_XREG64[loc.preg];

        if (strcmp(r, scratch) != 0)
            be_emitf(be, "    movapd %s, %s\n", scratch, r);
    }
    else
    {
        be_emitf(be, "    movsd %s, ", scratch);
        emit_spill_(be, f, loc.spill_slot);
        be_emitf(be, "\n");
    }
}

static void emit_store_freg_(backend_t*        be,
                             const ir_func_t*  f,
                             const ir_alloc_t* a,
                             ir_vreg_t         v,
                             const char*       scratch)
{
    ir_alloc_loc_t loc = a->vreg_locs[v.id];

    if (loc.kind == IR_LOC_REG)
    {
        const char* r = NASM_XREG64[loc.preg];

        if (strcmp(r, scratch) != 0)
            be_emitf(be, "    movapd %s, %s\n", r, scratch);
    }
    else
    {
        be_emitf(be, "    movsd ");
        emit_spill_(be, f, loc.spill_slot);
        be_emitf(be, ", %s\n", scratch);
    }
}

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
static long incoming_stack_arg_disp_(size_t stack_index)
{
    /*
        Prologue currently is:

            push rbp
            push r12
            push r13
            push r14
            push r15
            mov  rbp, rsp

        Therefore:
            [rbp + 0]  saved r15
            [rbp + 8]  saved r14
            [rbp + 16] saved r13
            [rbp + 24] saved r12
            [rbp + 32] old rbp
            [rbp + 40] return address
            [rbp + 48] first stack-passed arg
    */
    return (long)(48 + stack_index * NASM_WORD_BYTES);
}

static void emit_incoming_stack_arg_(backend_t* be, size_t stack_index)
{
    be_emitf(be, "[rbp+%ld]", incoming_stack_arg_disp_(stack_index));
}
static err_t nasm_emit_ir_func_(backend_t* be, const ir_func_t* f, const ir_alloc_t* a)
{
    size_t frame_slots = f->slot_count + a->spill_count + a->vreg_loc_count;
    size_t frame_bytes = align16_(frame_slots * NASM_WORD_BYTES);

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

    size_t iarg = 0;
    size_t farg = 0;
    size_t stack_arg = 0;

for (size_t i = 0; i < meta->param_count; ++i)
{
    if (meta->param_types[i] == AST_TYPE_FLOAT)
    {
        if (farg < NASM_XMM_ARG_REG_COUNT)
        {
            be_emitf(be, "    movsd ");
            emit_slot_(be, i);
            be_emitf(be, ", %s\n", NASM_XMM_ARG_REGS[farg++]);
        }
        else
        {
            be_emitf(be, "    movsd xmm0, ");
            emit_incoming_stack_arg_(be, stack_arg++);
            be_emitf(be, "\n");

            be_emitf(be, "    movsd ");
            emit_slot_(be, i);
            be_emitf(be, ", xmm0\n");
        }
    }
    else
    {
        if (iarg < NASM_INT_ARG_REG_COUNT)
        {
            be_emitf(be, "    mov  ");
            emit_slot_(be, i);
            be_emitf(be, ", %s\n", NASM_ARG_REGS[iarg++]);
        }
        else
        {
            be_emitf(be, "    mov  rax, ");
            emit_incoming_stack_arg_(be, stack_arg++);
            be_emitf(be, "\n");

            be_emitf(be, "    mov  ");
            emit_slot_(be, i);
            be_emitf(be, ", rax\n");
        }
    }
}    for (size_t ip = 0; ip < f->instr_count; ++ip)
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
static const char* jcc_false_i64_(token_kind_t op)
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

static const char* jcc_false_f64_(token_kind_t op)
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
        emit_slot_(be, in->slot);
        be_emitf(be, "\n");
    }
    else if (in->imm == -1)
    {
        be_emitf(be, "    dec  qword ");
        emit_slot_(be, in->slot);
        be_emitf(be, "\n");
    }
    else
    {
        be_emitf(be, "    add  qword ");
        emit_slot_(be, in->slot);
        be_emitf(be, ", %lld\n", (long long)in->imm);
    }
    return OK;
        case IR_OP_JMP:
            be_emitf(be, "    jmp  .L%zu\n", in->label_id);
            return OK;

        case IR_OP_JZ:
            emit_load_vreg_(be, f, a, in->a, "rax");
            be_emitf(be, "    test rax, rax\n");
            be_emitf(be, "    jz   .L%zu\n", in->label_id);
            return OK;

case IR_OP_MOV_IMM_I64:
    emit_mov_imm_i64_to_vreg_(be, f, a, in->dst, in->imm);
    return OK;
        case IR_OP_NEG_I64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            be_emitf(be, "    neg  rax\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_NOT_I64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            be_emitf(be, "    test rax, rax\n");
            be_emitf(be, "    sete al\n");
            be_emitf(be, "    movzx rax, al\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;
case IR_OP_JCC_FALSE_I64:
{
    const char* jcc = jcc_false_i64_((token_kind_t)in->imm);
    BE_CHECK(be, jcc != NULL, NULL, "Bad i64 branch condition");

    emit_load_vreg_(be, f, a, in->a, "rax");
    emit_load_vreg_(be, f, a, in->b, "rdx");

    be_emitf(be, "    cmp  rax, rdx\n");
    be_emitf(be, "    %s  .L%zu\n", jcc, in->label_id);
    return OK;
}

case IR_OP_JCC_FALSE_F64:
{
    const char* jcc = jcc_false_f64_((token_kind_t)in->imm);
    BE_CHECK(be, jcc != NULL, NULL, "Bad f64 branch condition");

    emit_load_freg_(be, f, a, in->a, "xmm0");
    emit_load_freg_(be, f, a, in->b, "xmm1");

    be_emitf(be, "    ucomisd xmm0, xmm1\n");
    be_emitf(be, "    %s  .L%zu\n", jcc, in->label_id);
    return OK;
}
        case IR_OP_MOV:
            if (in->dst.type == IR_TYPE_F64)
            {
                emit_load_freg_(be, f, a, in->a, "xmm0");
                emit_store_freg_(be, f, a, in->dst, "xmm0");
            }
            else
            {
                emit_load_vreg_(be, f, a, in->a, "rax");
                emit_store_vreg_(be, f, a, in->dst, "rax");
            }
            return OK;

case IR_OP_MOV_IMM_F64:
    emit_mov_imm_f64_to_vreg_(be, f, a, in->dst, in->imm);
    return OK;
case IR_OP_LOAD_SLOT:
    if (in->dst.type == IR_TYPE_F64)
        emit_load_slot_to_vreg_f64_(be, f, a, in->dst, in->slot);
    else
        emit_load_slot_to_vreg_i64_(be, f, a, in->dst, in->slot);
    return OK;
 case IR_OP_STORE_SLOT:
    if (in->a.type == IR_TYPE_F64)
        emit_store_slot_from_vreg_f64_(be, f, a, in->a, in->slot);
    else
        emit_store_slot_from_vreg_i64_(be, f, a, in->a, in->slot);
    return OK;
        case IR_OP_NEG_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            be_emitf(be, "    xorpd xmm0, [__f64_sign_mask]\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_I64_TO_F64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            be_emitf(be, "    cvtsi2sd xmm0, rax\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_F64_TO_I64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            be_emitf(be, "    cvttsd2si rax, xmm0\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        default:
            break;
    }

    return nasm_emit_ir_arith_or_call_(be, f, a, in, ip);
}

static err_t nasm_emit_ir_arith_or_call_(backend_t* be,
                                         const ir_func_t* f,
                                         const ir_alloc_t* a,
                                         const ir_instr_t* in,
                                         size_t ip)
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
            emit_load_vreg_(be, f, a, in->a, "rax");
            emit_load_vreg_(be, f, a, in->b, "rdx");
            be_emitf(be, "    add  rax, rdx\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_SUB_I64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            emit_load_vreg_(be, f, a, in->b, "rdx");
            be_emitf(be, "    sub  rax, rdx\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_MUL_I64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            emit_load_vreg_(be, f, a, in->b, "rdx");
            be_emitf(be, "    imul rax, rdx\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_DIV_I64:
            emit_load_vreg_(be, f, a, in->a, "rax");
            emit_load_vreg_(be, f, a, in->b, "rdi");
            be_emitf(be, "    cqo\n");
            be_emitf(be, "    idiv rdi\n");
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;
case IR_OP_POW_I64:
    emit_load_vreg_(be, f, a, in->a, "rdi");
    emit_load_vreg_(be, f, a, in->b, "rsi");
    be_emitf(be, "    call __brl_ipow_i64\n");
    emit_store_vreg_(be, f, a, in->dst, "rax");
    return OK;

        case IR_OP_CMP_EQ_I64:
        case IR_OP_CMP_NE_I64:
        case IR_OP_CMP_LT_I64:
        case IR_OP_CMP_GT_I64:
        case IR_OP_CMP_LE_I64:
        case IR_OP_CMP_GE_I64:
            return nasm_emit_cmp_(be, f, a, in);

case IR_OP_SQRT_F64:
    emit_save_live_regs_around_call_(be, f, a, ip);
    emit_load_freg_(be, f, a, in->a, "xmm0");
    be_emitf(be, "    call __brl_sqrt_f64\n");
    emit_restore_live_regs_around_call_(be, f, a, ip);
    emit_store_freg_(be, f, a, in->dst, "xmm0");
    return OK;

case IR_OP_POW_F64:
    emit_save_live_regs_around_call_(be, f, a, ip);
    emit_load_freg_(be, f, a, in->a, "xmm0");
    emit_load_freg_(be, f, a, in->b, "xmm1");
    be_emitf(be, "    call __brl_fpow_f64\n");
    emit_restore_live_regs_around_call_(be, f, a, ip);
    emit_store_freg_(be, f, a, in->dst, "xmm0");
    return OK;
        case IR_OP_CALL:
            return nasm_emit_call_(be, f, a, in, ip);

case IR_OP_RET:
    if (in->a.id != IR_NO_VREG)
    {
        if (in->a.type == IR_TYPE_F64)
            emit_load_freg_(be, f, a, in->a, "xmm0");
        else
            emit_load_vreg_(be, f, a, in->a, "rax");
    }

    be_emitf(be, "    jmp  .L_epilogue_%s\n",
             ast_name_cstr(be->tree, f->name_id));
    return OK;        
case IR_OP_PRINTF_I64:
            return nasm_emit_printf_i64_(be, f, a, in, ip);

        case IR_OP_ADD_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            emit_load_freg_(be, f, a, in->b, "xmm1");
            be_emitf(be, "    addsd xmm0, xmm1\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_SUB_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            emit_load_freg_(be, f, a, in->b, "xmm1");
            be_emitf(be, "    subsd xmm0, xmm1\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_MUL_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            emit_load_freg_(be, f, a, in->b, "xmm1");
            be_emitf(be, "    mulsd xmm0, xmm1\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_DIV_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            emit_load_freg_(be, f, a, in->b, "xmm1");
            be_emitf(be, "    divsd xmm0, xmm1\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_CMP_EQ_F64:
        case IR_OP_CMP_NE_F64:
        case IR_OP_CMP_LT_F64:
        case IR_OP_CMP_GT_F64:
        case IR_OP_CMP_LE_F64:
        case IR_OP_CMP_GE_F64:
            return nasm_emit_fcmp_(be, f, a, in);

         case IR_OP_FLOOR_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_floor_f64\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_CEIL_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_ceil_f64\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_ROUND_F64:
            emit_load_freg_(be, f, a, in->a, "xmm0");
            be_emitf(be, "    call __brl_round_f64\n");
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK; 
        case IR_OP_PRINTF_F64:
            return nasm_emit_printf_f64_(be, f, a, in, ip);

        case IR_OP_PUTCHAR_I64:
        case IR_OP_SCANF_I64:
        case IR_OP_SCANF_F64:
        case IR_OP_GETCHAR_I64:
        case IR_OP_RUNTIME_DRAW:
        case IR_OP_RUNTIME_CLEAN:
        case IR_OP_RUNTIME_SET_PIXEL:
            return nasm_emit_runtime_call_(be, f, a, in, ip);

        default:
            BE_FAIL_NODE(be, NULL, "NASM IR emit: unsupported op %s", ir_op_to_cstr(in->op));
    }
}

static err_t nasm_emit_cmp_(backend_t* be,
                            const ir_func_t* f,
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

    emit_load_vreg_(be, f, a, in->a, "rax");
    emit_load_vreg_(be, f, a, in->b, "rdx");

    be_emitf(be, "    cmp  rax, rdx\n");
    be_emitf(be, "    %s al\n", setcc);
    be_emitf(be, "    movzx rax, al\n");

    emit_store_vreg_(be, f, a, in->dst, "rax");
    return OK;
}

static err_t nasm_emit_fcmp_(backend_t* be,
                             const ir_func_t* f,
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

    emit_load_freg_(be, f, a, in->a, "xmm0");
    emit_load_freg_(be, f, a, in->b, "xmm1");

    be_emitf(be, "    ucomisd xmm0, xmm1\n");
    be_emitf(be, "    %s al\n", setcc);
    be_emitf(be, "    movzx rax, al\n");

    emit_store_vreg_(be, f, a, in->dst, "rax");
    return OK;
}
static void compute_stack_arg_flags_(const func_meta_t* callee,
                                     unsigned char* stack_flags,
                                     size_t* out_stack_count)
{
    size_t iarg = 0;
    size_t farg = 0;
    size_t stack_count = 0;

    for (size_t i = 0; i < callee->param_count; ++i)
    {
        int goes_stack = 0;

        if (callee->param_types[i] == AST_TYPE_FLOAT)
        {
            if (farg < NASM_XMM_ARG_REG_COUNT)
                farg++;
            else
            {
                farg++;
                goes_stack = 1;
            }
        }
        else
        {
            if (iarg < NASM_INT_ARG_REG_COUNT)
                iarg++;
            else
            {
                iarg++;
                goes_stack = 1;
            }
        }

        stack_flags[i] = (unsigned char)goes_stack;
        stack_count += (size_t)goes_stack;
    }

    if (out_stack_count)
        *out_stack_count = stack_count;
}
static err_t nasm_emit_call_(backend_t* be,
                             const ir_func_t* f,
                             const ir_alloc_t* a,
                             const ir_instr_t* in,
                             size_t ip)
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

    emit_save_live_regs_around_call_(be, f, a, ip);

    size_t stack_pad = (stack_count % 2) ? NASM_WORD_BYTES : 0;

    if (stack_pad)
        be_emitf(be, "    sub  rsp, 8 ; call stack alignment pad\n");

    for (size_t i = in->arg_count; i-- > 0; )
    {
        if (!stack_flags[i])
            continue;

        if (in->args[i].type == IR_TYPE_F64)
        {
            emit_load_freg_(be, f, a, in->args[i], "xmm0");
            be_emitf(be, "    sub  rsp, 8\n");
            be_emitf(be, "    movsd [rsp], xmm0\n");
        }
        else
        {
            emit_load_vreg_(be, f, a, in->args[i], "rax");
            be_emitf(be, "    push rax\n");
        }
    }

    size_t iarg = 0;
    size_t farg = 0;

    for (size_t i = 0; i < in->arg_count; ++i)
    {
        if (stack_flags[i])
            continue;

        if (callee->param_types[i] == AST_TYPE_FLOAT)
        {
            BE_CHECK(be, farg < NASM_XMM_ARG_REG_COUNT,
                     NULL, "Internal: bad float arg classification");

            emit_load_freg_(be, f, a, in->args[i], NASM_XMM_ARG_REGS[farg++]);
        }
        else
        {
            BE_CHECK(be, iarg < NASM_INT_ARG_REG_COUNT,
                     NULL, "Internal: bad int arg classification");

            emit_load_vreg_(be, f, a, in->args[i], NASM_ARG_REGS[iarg++]);
        }
    }

    be_emitf(be, "    call %s\n", callee->label);

    if (stack_count || stack_pad)
        be_emitf(be, "    add  rsp, %zu\n",
                 stack_count * NASM_WORD_BYTES + stack_pad);

    emit_restore_live_regs_around_call_(be, f, a, ip);

    if (in->dst.id != IR_NO_VREG)
    {
        if (in->dst.type == IR_TYPE_F64)
            emit_store_freg_(be, f, a, in->dst, "xmm0");
        else
            emit_store_vreg_(be, f, a, in->dst, "rax");
    }

    return OK;
}

static err_t nasm_emit_printf_i64_(backend_t* be,
                                   const ir_func_t* f,
                                   const ir_alloc_t* a,
                                   const ir_instr_t* in,
                                   size_t ip)
{
    emit_save_live_regs_around_call_(be, f, a, ip);

    emit_load_vreg_(be, f, a, in->a, "rdi");
    be_emitf(be, "    call __brl_print_i64\n");

    emit_restore_live_regs_around_call_(be, f, a, ip);

    return OK;
}
static err_t nasm_emit_printf_f64_(backend_t* be,
                                   const ir_func_t* f,
                                   const ir_alloc_t* a,
                                   const ir_instr_t* in,
                                   size_t ip)
{
    emit_save_live_regs_around_call_(be, f, a, ip);

    emit_load_freg_(be, f, a, in->a, "xmm0");
    be_emitf(be, "    call __brl_print_f64\n");

    emit_restore_live_regs_around_call_(be, f, a, ip);

    return OK;
}
static err_t nasm_emit_runtime_call_(backend_t* be,
                                     const ir_func_t* f,
                                     const ir_alloc_t* a,
                                     const ir_instr_t* in,
                                     size_t ip)
{
    emit_save_live_regs_around_call_(be, f, a, ip);

    switch (in->op)
    {
        case IR_OP_SCANF_I64:
            be_emitf(be, "    call __brl_scan_i64\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_SCANF_F64:
            be_emitf(be, "    call __brl_scan_f64\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            emit_store_freg_(be, f, a, in->dst, "xmm0");
            return OK;

        case IR_OP_GETCHAR_I64:
            be_emitf(be, "    call __brl_getchar\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            emit_store_vreg_(be, f, a, in->dst, "rax");
            return OK;

        case IR_OP_PUTCHAR_I64:
            emit_load_vreg_(be, f, a, in->a, "rdi");
            be_emitf(be, "    call __brl_putchar\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            return OK;
        case IR_OP_RUNTIME_DRAW:
            be_emitf(be, "    call __brl_draw\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            return OK;

        case IR_OP_RUNTIME_CLEAN:
            be_emitf(be, "    call __brl_clean_vm\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            return OK;

        case IR_OP_RUNTIME_SET_PIXEL:
            emit_load_vreg_(be, f, a, in->args[0], "rdi");
            emit_load_vreg_(be, f, a, in->args[1], "rsi");
            emit_load_vreg_(be, f, a, in->args[2], "rdx");
            be_emitf(be, "    call __brl_set_pixel\n");
            emit_restore_live_regs_around_call_(be, f, a, ip);
            return OK;

        default:
            break;
    }

    BE_FAIL_NODE(be, NULL, "Bad runtime op %s", ir_op_to_cstr(in->op));
}

static void emit_restore_live_regs_around_call_(backend_t* be,
                                                const ir_func_t* f,
                                                const ir_alloc_t* a,
                                                size_t ip)
{
    for (size_t v = 0; v < a->vreg_loc_count; ++v)
    {
        ir_alloc_loc_t loc = a->vreg_locs[v];

        if (loc.kind != IR_LOC_REG)
            continue;

        if (!interval_live_across_call_(a, v, ip))
            continue;

        if (a->intervals[v].type == IR_TYPE_F64)
        {
            if (!nasm_xmm_preg_is_caller_saved_(loc.preg))
                continue;

            be_emitf(be, "    movsd %s, ", NASM_XREG64[loc.preg]);
            emit_call_save_slot_(be, f, a, v);
            be_emitf(be, "\n");
            continue;
        }

        if (!nasm_gpr_preg_is_caller_saved_(loc.preg))
            continue;

        be_emitf(be, "    mov  %s, ", NASM_PREG64[loc.preg]);
        emit_call_save_slot_(be, f, a, v);
        be_emitf(be, "\n");
    }
}
static void emit_save_live_regs_around_call_(backend_t* be,
                                             const ir_func_t* f,
                                             const ir_alloc_t* a,
                                             size_t ip)
{
    for (size_t v = 0; v < a->vreg_loc_count; ++v)
    {
        ir_alloc_loc_t loc = a->vreg_locs[v];

        if (loc.kind != IR_LOC_REG)
            continue;

        if (!interval_live_across_call_(a, v, ip))
            continue;

        if (a->intervals[v].type == IR_TYPE_F64)
        {
            if (!nasm_xmm_preg_is_caller_saved_(loc.preg))
                continue;

            be_emitf(be, "    movsd ");
            emit_call_save_slot_(be, f, a, v);
            be_emitf(be, ", %s\n", NASM_XREG64[loc.preg]);
            continue;
        }

        if (!nasm_gpr_preg_is_caller_saved_(loc.preg))
            continue;

        be_emitf(be, "    mov  ");
        emit_call_save_slot_(be, f, a, v);
        be_emitf(be, ", %s\n", NASM_PREG64[loc.preg]);
    }
}


