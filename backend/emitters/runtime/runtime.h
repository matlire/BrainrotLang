#ifndef RUNTIME_H
#define RUNTIME_H

#include "backend/emitters/nasm_internal.h"

void nasm_collect_runtime_use(nasm_runtime_use_t* rt, const ir_func_t* f);
void nasm_emit_runtime       (backend_t* be, const nasm_runtime_use_t* rt);

void nasm_emit_runtime_io      (backend_t* be, const nasm_runtime_use_t* rt);
void nasm_emit_runtime_math    (backend_t* be, const nasm_runtime_use_t* rt);
void nasm_emit_runtime_graphics(backend_t* be, const nasm_runtime_use_t* rt);

#endif
