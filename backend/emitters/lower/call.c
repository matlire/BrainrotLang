#include "backend/emitters/runtime/runtime.h"
#include "backend/emitters/lower/lower.h"

#ifndef NASM_TRY
#define NASM_TRY(x) do { err_t rc__ = (x); if (rc__ != OK) return rc__; } while (0)
#endif

#ifndef NASM_EMIT
#define NASM_EMIT(l, ...) ir_emit((l)->f, (ir_instr_t){ __VA_ARGS__ })
#endif

int nasm_is_builtin_name(const char* name)
{
    return be_streq(name, "in")        || be_streq(name, "fin")      || be_streq(name, "cin")    ||
           be_streq(name, "draw")      || be_streq(name, "clean_vm")                             ||
           be_streq(name, "out")       || be_streq(name, "fout")     || be_streq(name, "cout")   ||
           be_streq(name, "set_pixel") || be_streq(name, "sqrt")                                  ||
           be_streq(name, "pow")       || be_streq(name, "mpow")     || be_streq(name, "cap")    ||
           be_streq(name, "nocap")     || be_streq(name, "stinky")                                ||
           be_streq(name, "gyat")      || be_streq(name, "skibidi")                              ||
           be_streq(name, "pookie")    || be_streq(name, "rizz")     || be_streq(name, "menace");
}

const ast_node_t* nasm_ast_arg_at(const ast_node_t* args, size_t idx)
{
    const ast_node_t* a = args ? args->left : NULL;
    while (a && idx--)
        a = a->right;
    return a;
}

static err_t emit_zero_i64_(nasm_lower_t* l, token_pos_t pos, ir_vreg_t* out)
{
    *out = ir_new_vreg(l->f, IR_TYPE_I64);
    return NASM_EMIT(l, .op = IR_OP_MOV_IMM_I64, .dst = *out, .imm = 0, .pos = pos);
}

err_t nasm_lower_call_expr_i64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    return nasm_is_builtin_name(name) ? nasm_lower_builtin_i64(l, call, out) :
                                        nasm_lower_user_call(l, call, out);
}

err_t nasm_lower_call_expr_f64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char* name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    return nasm_is_builtin_name(name) ? nasm_lower_builtin_f64(l, call, out) :
                                        nasm_lower_user_call(l, call, out);
}

#ifndef __NASM_SIM_GRAPHICS
#define NASM_REQUIRE_GRAPHICS(call_node) \
    BE_FAIL_NODE(l->be, (call_node), "graphics builtin requires build flag NASM_GRAPHICS=1 / --graphics")
#else
#define NASM_REQUIRE_GRAPHICS(call_node) ((void)0)
#endif

err_t nasm_lower_builtin_i64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const char*       name = ast_name_cstr(l->be->tree, call->u.call.name_id);
    const ast_node_t* args = call->left;
    size_t            argc = nasm_ast_arg_count(args);

    if (be_streq(name, "in") || be_streq(name, "cap"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        *out = ir_new_vreg(l->f, IR_TYPE_I64);
        return NASM_EMIT(l, .op = IR_OP_SCANF_I64, .dst = *out, .pos = call->pos);
    }

    if (be_streq(name, "cin") || be_streq(name, "stinky"))
    {
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);

        *out = ir_new_vreg(l->f, IR_TYPE_I64);
        return NASM_EMIT(l, .op = IR_OP_GETCHAR_I64, .dst = *out, .pos = call->pos);
    }

    if (be_streq(name, "out") || be_streq(name, "pookie"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        NASM_TRY(nasm_lower_expr_i64(l, nasm_ast_arg_at(args, 0), out));
        return NASM_EMIT(l, .op = IR_OP_PRINTF_I64, .a = *out, .pos = call->pos);
    }

    if (be_streq(name, "cout") || be_streq(name, "menace"))
    {
        BE_CHECK(l->be, argc == 1, call, "%s(x) takes 1 arg", name);

        NASM_TRY(nasm_lower_expr_i64(l, nasm_ast_arg_at(args, 0), out));
        return NASM_EMIT(l, .op = IR_OP_PUTCHAR_I64, .a = *out, .pos = call->pos);
    }

    if (be_streq(name, "draw") || be_streq(name, "gyat"))
    {
        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);
        NASM_TRY(NASM_EMIT(l, .op = IR_OP_RUNTIME_DRAW, .pos = call->pos));
        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "clean_vm") || be_streq(name, "skibidi"))
    {
        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 0, call, "%s() takes 0 args", name);
        NASM_TRY(NASM_EMIT(l, .op = IR_OP_RUNTIME_CLEAN, .pos = call->pos));
        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "set_pixel"))
    {
        ir_instr_t in = { .op = IR_OP_RUNTIME_SET_PIXEL, .arg_count = 3, .pos = call->pos };

        NASM_REQUIRE_GRAPHICS(call);
        BE_CHECK(l->be, argc == 3, call, "set_pixel(x,y,ch) takes 3 args");

        for (size_t i = 0; i < 3; ++i)
            NASM_TRY(nasm_lower_expr_i64(l, nasm_ast_arg_at(args, i), &in.args[i]));

        NASM_TRY(ir_emit(l->f, in));
        return emit_zero_i64_(l, call->pos, out);
    }

    if (be_streq(name, "fin") || be_streq(name, "nocap") ||
        be_streq(name, "fout") || be_streq(name, "rizz"))
    {
        ir_vreg_t fv = { 0 };

        NASM_TRY(nasm_lower_builtin_f64(l, call, &fv));

        *out = ir_new_vreg(l->f, IR_TYPE_I64);
        return NASM_EMIT(l, .op = IR_OP_F64_TO_I64, .dst = *out, .a = fv, .pos = call->pos);
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

err_t nasm_lower_user_call(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out)
{
    const func_meta_t* fm   = be_find_func(l->be, call->u.call.name_id);
    const ast_node_t*  args = call->left;
    size_t             argc = nasm_ast_arg_count(args);
    ir_instr_t         in   = {
        .op        = IR_OP_CALL,
        .dst       = { .id = IR_NO_VREG, .type = IR_TYPE_VOID },
        .func_id   = call->u.call.name_id,
        .arg_count = argc,
        .pos       = call->pos,
    };

    BE_CHECK(l->be, fm != NULL, call, "Unknown function call");
    BE_CHECK(l->be, argc == fm->param_count, call, "Call arg count mismatch");
    BE_CHECK(l->be, argc <= IR_MAX_CALL_ARGS, call, "IR supports at most %d total args", IR_MAX_CALL_ARGS);

    if (fm->ret_type != AST_TYPE_VOID)
        in.dst = ir_new_vreg(l->f, ir_type_from_ast_(fm->ret_type));

    for (size_t i = 0; i < argc; ++i)
    {
        const ast_node_t* a = nasm_ast_arg_at(args, i);
        NASM_TRY(fm->param_types[i] == AST_TYPE_FLOAT ? nasm_lower_expr_f64(l, a, &in.args[i]) :
                                                        nasm_lower_expr_i64(l, a, &in.args[i]));
    }

    NASM_TRY(ir_emit(l->f, in));
    if (out) *out = in.dst;
    return OK;
}
