#ifndef BACKEND_INTERNAL_H
#define BACKEND_INTERNAL_H

#include "backend.h"
#include "libs/types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BE_VEC_GROW(ptr, cap, want, type)                      \
    block_begin                                                \
        if ((want) > (cap)) {                                  \
            size_t new_cap = (cap) ? (cap) * 2 : 8;            \
            while (new_cap < (want)) new_cap *= 2;             \
            void* np = realloc((ptr), new_cap * sizeof(type)); \
            if (!np) return ERR_ALLOC;                         \
            (ptr) = (type*)np;                                 \
            (cap) = new_cap;                                   \
        }                                                      \
    block_end

#define BE_FAIL_NODE(be, nodeptr, fmt, ...)                                          \
    block_begin                                                                      \
        const ast_node_t* __be_node = (const ast_node_t*)(nodeptr);                  \
        token_pos_t       __be_pos  = __be_node ? __be_node->pos : (token_pos_t){0}; \
        be_set_error((be), __be_pos, __be_pos.offset,                                \
                     (fmt), ##__VA_ARGS__);                                          \
        return ERR_SYNTAX;                                                           \
    block_end

#define BE_CHECK(be, cond, node, fmt, ...)                    \
    block_begin                                               \
        if (!(cond))                                          \
            BE_FAIL_NODE((be), (node), (fmt), ##__VA_ARGS__); \
    block_end

void be_set_error(backend_t*  be,
                  token_pos_t pos,
                  size_t      fallback_offset,
                  const char* fmt, ...);

int   be_emitf        (backend_t* be, const char* fmt, ...);
char* be_strdup_printf(const char* fmt, ...);

char* be_new_label(backend_t* be, const char* prefix, const char* label_prefix);

size_t             be_find_name(const ast_tree_t* tree, const char* name);
const func_meta_t* be_find_func(const backend_t* be, size_t name_id);

err_t be_collect_funcs(backend_t*        be,
                       const ast_node_t* program,
                       const char*       label_fmt);

void be_free(backend_t* be);

ssize_t be_bind_lookup(const backend_t* be, size_t name_id);
err_t   be_bind_push (backend_t* be,
                      size_t     name_id,
                      ast_type_t type,
                      size_t     offset,
                      size_t     depth);

void    be_bind_pop_depth(backend_t* be, size_t depth);

int be_streq(const char* a, const char* b);

#endif
