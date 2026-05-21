#ifndef NASM_LOWER_H
#define NASM_LOWER_H

#include "backend/emitters/nasm_internal.h"

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

err_t nasm_lower_func(backend_t*        be,
                      const ast_node_t* fn,
                      ir_func_t*        out);

ast_type_t nasm_infer_expr_type(backend_t*        be,
                                const ast_node_t* e);

int nasm_ast_i64_literal(const ast_node_t* n,
                         i64_t*            out);

err_t nasm_lower_stmt     (nasm_lower_t* l, const ast_node_t* st);
err_t nasm_lower_block    (nasm_lower_t* l, const ast_node_t* block);
err_t nasm_lower_vdecl    (nasm_lower_t* l, const ast_node_t* vd);
err_t nasm_lower_assign   (nasm_lower_t* l, const ast_node_t* asn);
err_t nasm_lower_return   (nasm_lower_t* l, const ast_node_t* ret);
err_t nasm_lower_expr_stmt(nasm_lower_t* l, const ast_node_t* st);
err_t nasm_lower_call_stmt(nasm_lower_t* l, const ast_node_t* st);
err_t nasm_lower_print_i64(nasm_lower_t* l, const ast_node_t* pr);
err_t nasm_lower_print_f64(nasm_lower_t* l, const ast_node_t* pr);

err_t nasm_lower_expr_i64           (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_unary_i64          (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_binary_i64         (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_expr_f64_to_i64    (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_logical_and_i64    (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_logical_or_i64     (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_float_cmp_i64      (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t*       out);
err_t nasm_lower_i64_pow_small_const(nasm_lower_t* l, const ast_node_t* base, i64_t          exp_i, token_pos_t pos,ir_vreg_t* out);

err_t nasm_lower_expr_f64   (nasm_lower_t* l, const ast_node_t* e,    ir_vreg_t* out);
err_t nasm_lower_builtin_f64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);

size_t nasm_ast_arg_count        (const ast_node_t* args);
const ast_node_t* nasm_ast_arg_at(const ast_node_t* args, size_t            idx);
int nasm_is_builtin_name         (const char* name);

err_t nasm_lower_call_expr_i64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
err_t nasm_lower_call_expr_f64(nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
err_t nasm_lower_builtin_i64  (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);
err_t nasm_lower_user_call    (nasm_lower_t* l, const ast_node_t* call, ir_vreg_t* out);

err_t nasm_lower_cond_false(nasm_lower_t* l, const ast_node_t* cond, size_t false_label);
err_t nasm_lower_jz        (nasm_lower_t* l, const ast_node_t* cond, size_t false_label);
err_t nasm_lower_while     (nasm_lower_t* l, const ast_node_t* w);
err_t nasm_lower_break     (nasm_lower_t* l, const ast_node_t* brk);
err_t nasm_lower_if        (nasm_lower_t* l, const ast_node_t* ifn);
err_t nasm_lower_if_tail   (nasm_lower_t* l, const ast_node_t* tail, size_t L_end);
err_t nasm_lower_loop_push (nasm_lower_t* l, size_t            label);

void nasm_lower_loop_pop(nasm_lower_t* l);

int nasm_is_cmp_op (token_kind_t op);
int nasm_is_bool_op(token_kind_t op);

#endif
