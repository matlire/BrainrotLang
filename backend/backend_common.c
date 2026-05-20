#include "backend_internal.h"

void be_set_error(backend_t*  be,
                  token_pos_t pos,
                  size_t      fallback_offset,
                  const char* fmt, ...)
{
    if (!be || !be->op)
        return;

    be->op->error_pos = (pos.offset != 0 ? pos.offset : fallback_offset);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(be->op->error_msg, sizeof(be->op->error_msg), fmt, ap);
    va_end(ap);

    size_t len = strlen(be->op->error_msg);
    snprintf(be->op->error_msg + len,
             sizeof(be->op->error_msg) - len,
             " at %zu:%zu (offset: %zu)",
             pos.line,
             pos.column,
             be->op->error_pos);
}

int be_emitf(backend_t* be, const char* fmt, ...)
{
    if (!be || !be->op || !be->op->out_file)
        return 0;

    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(be->op->out_file, fmt, ap);
    va_end(ap);

    return r;
}

char* be_strdup_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (need < 0)
        return NULL;

    char* s = (char*)calloc((size_t)need + 1, 1);
    if (!s)
        return NULL;

    va_start(ap, fmt);
    vsnprintf(s, (size_t)need + 1, fmt, ap);
    va_end(ap);

    return s;
}

int be_streq(const char* a, const char* b)
{
    return a && b && strcmp(a, b) == 0;
}

char* be_new_label(backend_t* be, const char* prefix, const char* label_prefix)
{
    if (!be)
        return NULL;

    return be_strdup_printf("%s%s_%zu",
                            label_prefix ? label_prefix : "L_",
                            prefix       ? prefix : "x",
                            be->label_counter++);
}

static void be_count_locals_rec(const ast_node_t* n, size_t* out)
{
    if (!n || !out)
        return;

    if (n->kind == ASTK_VAR_DECL)
        (*out)++;

    for (const ast_node_t* c = n->left; c; c = c->right)
        be_count_locals_rec(c, out);
}

size_t be_find_name(const ast_tree_t* tree, const char* name)
{
    if (!tree || !name)
        return SIZE_MAX;

    const size_t len  = strlen(name);
    const size_t hash = sdbm_n(name, len);

    for (size_t i = 0; i < tree->nametable.amount; ++i)
    {
        const nametable_entry_t* entry = &tree->nametable.data[i];

        if (entry->hash != hash)
            continue;

        if (entry->length != len)
            continue;

        if (entry->name && memcmp(entry->name, name, len) == 0)
            return i;
    }

    return SIZE_MAX;
}

const func_meta_t* be_find_func(const backend_t* be, size_t name_id)
{
    if (!be)
        return NULL;

    for (size_t i = 0; i < be->func_amount; ++i)
        if (be->funcs[i].name_id == name_id)
            return &be->funcs[i];

    return NULL;
}

err_t be_collect_funcs(backend_t*        be,
                       const ast_node_t* program,
                       const char*       label_fmt)
{
    if (!be || !program)
        return ERR_BAD_ARG;

    BE_CHECK(be, program->kind == ASTK_PROGRAM,
             program, "Internal: root is not PROGRAM");

    for (const ast_node_t* fn = program->left; fn; fn = fn->right)
    {
        BE_CHECK(be, fn->kind == ASTK_FUNC,
                 fn, "Internal: PROGRAM child is not FUNC");

        size_t name_id = fn->u.func.name_id;
        const char* name = ast_name_cstr(be->tree, name_id);

        BE_CHECK(be, name != NULL,
                 fn, "Internal: function has invalid name id");

        BE_CHECK(be, be_find_func(be, name_id) == NULL,
                 fn, "Duplicate function '%s'", name);

        const ast_node_t* plist = fn->left;
        BE_CHECK(be, plist && plist->kind == ASTK_PARAM_LIST,
                 fn, "Internal: FUNC missing PARAM_LIST");

        size_t pcount = 0;
        for (const ast_node_t* p = plist->left; p; p = p->right)
            pcount++;

        ast_type_t* ptypes = NULL;

        if (pcount)
        {
            ptypes = (ast_type_t*)calloc(pcount, sizeof(*ptypes));
            if (!ptypes)
                return ERR_ALLOC;

            size_t i = 0;
            for (const ast_node_t* p = plist->left; p; p = p->right)
            {
                if (p->kind != ASTK_PARAM)
                {
                    free(ptypes);
                    BE_FAIL_NODE(be, p, "Internal: PARAM_LIST child is not PARAM");
                }

                ptypes[i++] = p->u.param.type;
            }
        }

        size_t locals = 0;
        const ast_node_t* body = plist->right;
        if (body)
            be_count_locals_rec(body, &locals);

        char* label = be_strdup_printf(label_fmt, name);
        if (!label)
        {
            free(ptypes);
            return ERR_ALLOC;
        }

        BE_VEC_GROW(be->funcs, be->func_cap, be->func_amount + 1, func_meta_t);

        be->funcs[be->func_amount++] = (func_meta_t){
            .name_id     = name_id,
            .label       = label,
            .ret_type    = fn->u.func.ret_type,
            .param_count = pcount,
            .param_types = ptypes,
            .local_count = locals,
        };
    }

    return OK;
}

ssize_t be_bind_lookup(const backend_t* be, size_t name_id)
{
    if (!be)
        return -1;

    for (ssize_t i = (ssize_t)be->bind_amount - 1; i >= 0; --i)
        if (be->binds[(size_t)i].name_id == name_id)
            return i;

    return -1;
}

err_t be_bind_push(backend_t* be,
                   size_t     name_id,
                   ast_type_t type,
                   size_t     offset,
                   size_t     depth)
{
    BE_VEC_GROW(be->binds, be->bind_cap, be->bind_amount + 1, binding_t);

    be->binds[be->bind_amount++] = (binding_t){
        .name_id = name_id,
        .type    = type,
        .offset  = offset,
        .depth   = depth,
    };

    return OK;
}

void be_bind_pop_depth(backend_t* be, size_t depth)
{
    if (!be)
        return;

    while (be->bind_amount > 0 &&
           be->binds[be->bind_amount - 1].depth == depth)
    {
        be->bind_amount--;
    }
}

void be_free(backend_t* be)
{
    if (!be)
        return;

    for (size_t i = 0; i < be->func_amount; ++i)
    {
        free(be->funcs[i].label);
        free(be->funcs[i].param_types);
    }

    free(be->funcs);
    free(be->binds);

    for (size_t i = 0; i < be->loop_amount; ++i)
        free(be->loops[i].end_label);

    free(be->loops);
    free(be->fn_end_label);

    memset(be, 0, sizeof(*be));
}
