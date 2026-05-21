#include "backend/emitters/runtime/runtime.h"


void nasm_collect_runtime_use(nasm_runtime_use_t* rt, const ir_func_t* f)
{
    if (!rt || !f)
        return;

#define RT(op, ...)             \
        case op:                \
            __VA_ARGS__;        \
            break

    for (size_t i = 0; i < f->instr_count; ++i)
    {
        const ir_instr_t* in = &f->instrs[i];

        switch (in->op)
        {
            RT(IR_OP_PRINTF_I64,
               rt->print_i64 = 1);

            RT(IR_OP_PRINTF_F64,
               rt->print_f64 = 1;
               rt->print_i64_no_nl = 1;
               rt->putchar_ = 1);

            RT(IR_OP_PUTCHAR_I64,
               rt->putchar_ = 1);

            RT(IR_OP_SCANF_I64,
               rt->scan_i64 = 1;
               rt->getchar_ = 1;
               rt->ungetchar_ = 1);

            RT(IR_OP_SCANF_F64,
               rt->scan_f64 = 1;
               rt->getchar_ = 1;
               rt->ungetchar_ = 1);

            RT(IR_OP_GETCHAR_I64,
               rt->getchar_ = 1);

            RT(IR_OP_FLOOR_F64,
               rt->floor_f64 = 1);

            RT(IR_OP_CEIL_F64,
               rt->ceil_f64 = 1);

            RT(IR_OP_ROUND_F64,
               rt->round_f64 = 1;
               rt->floor_f64 = 1);

            RT(IR_OP_SQRT_F64,
               rt->sqrt_f64 = 1);

            RT(IR_OP_POW_F64,
               rt->fpow_f64 = 1);

            RT(IR_OP_RUNTIME_CLEAN,
               rt->clean_vm = 1);

            RT(IR_OP_RUNTIME_SET_PIXEL,
               rt->set_pixel = 1);

            RT(IR_OP_RUNTIME_DRAW,
               rt->draw = 1);

            RT(IR_OP_POW_I64,
               rt->ipow_i64 = 1);

            default:
                break;
        }
    }

#undef RT
}

void nasm_emit_runtime(backend_t* be, const nasm_runtime_use_t* rt)
{
    if (!rt)
        return;

    nasm_emit_runtime_math(be, rt);
    nasm_emit_runtime_io(be, rt);
    nasm_emit_runtime_graphics(be, rt);
}
