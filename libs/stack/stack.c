#include "stack.h"

typedef struct
{
    stack_info_t   stack_info;
    element_info_t elem_info;

    void*  data;
    size_t size;
    size_t capacity;

    void*  raw_data;
    size_t alloc_size;

    stack_print_fn  printer; 
    stack_sprint_fn sprinter; 
} stack_t;

const char * const err_msgs[] = {
    "OK", "ERR BAD ARG", "ERR CORRUPT", "ERR ALLOC"
};

const char* err_str(const err_t e)
{
    return err_msgs[e];
}

static stack_t** stack_array     = NULL;
static size_t    stack_array_cap = 0;

static inline int id_in_range(stack_id id)
{
    return (stack_array && id < stack_array_cap);
}

static inline stack_t* get_stack(stack_id id)
{
    return (id_in_range(id) ? stack_array[id] : NULL);
}

static int free_slot(stack_id slot)
{
    if (!CHECK(ERROR, id_in_range(slot), "free_slot: stack_id incorrect")) return 1;
    free(stack_array[slot]);
    stack_array[slot] = NULL;
    return 0;
}

static int ensure_registry(void)
{
    if (stack_array == NULL || stack_array_cap == 0)
    {
        stack_array_cap = INITIAL_CAPACITY;
        stack_array = (stack_t**) calloc(stack_array_cap, sizeof(*stack_array));
        if (!CHECK(ERROR, stack_array != NULL, "ensure_registry: failed to allocate stack_array")) return 0;
        return 1;
    }
    return 0;
}

static stack_id alloc_slot(void)
{
    ensure_registry();

    for (stack_id i = 0; i < stack_array_cap; i++)
    {
        if (stack_array[i] == NULL) return i;
    }
  
    size_t old_cap = stack_array_cap;
    size_t new_cap = old_cap ? old_cap * 2 : INITIAL_CAPACITY;
    void* p = realloc(stack_array, new_cap * sizeof(*stack_array));
    if (!CHECK(ERROR, p != NULL, "alloc_slot: failed reallocate stack_array")) return 0;

    stack_array = (stack_t**)p;
    memset(stack_array + old_cap, 0, (new_cap - old_cap) * sizeof(*stack_array));
    stack_array_cap = new_cap;

    return (stack_id)old_cap;
}

static size_t round_up(const size_t n, const size_t a)
{
    if (a == 0) return n;
    size_t r = n % a;
    return r ? (n + (a - r)) : n;
}

static inline void* calculate_ptr(const stack_t * const st, const size_t i)
{
    return (void*)((unsigned char*)st->data + i * st->elem_info.elem_stride);
}

static inline void* back_canary_ptr(const stack_t* st)
{
    return (void*)((unsigned char*)st->raw_data + st->alloc_size - sizeof(STACK_CANARY));
}

static err_t stack_realloc(stack_id stack, const size_t new_capacity)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG, 
                         "stack_realloc: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT,
                    "stack_realloc: st == NULL");

    STACK_VERIFY(stack);

    if (new_capacity == st->capacity) return OK;

    size_t new_bytes = new_capacity * st->elem_info.elem_stride + 2 * sizeof(STACK_CANARY);

    if (new_capacity == 0)
    {
        free(st->raw_data);
        st->data       = NULL;
        st->raw_data   = NULL;
        st->capacity   = 0;
        st->alloc_size = 0;
        if (st->size > 0) st->size = 0;
        return OK;
    }

    void* p = (void*) realloc(st->raw_data, new_bytes);
    STACK_ERR_CHECK(ERROR, p != NULL, stack, ERR_ALLOC,
                    "stack_realloc: realloc failed");

    st->raw_data = p;
    st->data     = (void*)((unsigned char*)p + sizeof(STACK_CANARY));

    if (new_capacity > st->capacity){
        const size_t old_bytes = st->capacity * st->elem_info.elem_stride;
        memset((unsigned char*)st->data + old_bytes, 0, new_bytes - old_bytes - 2 * sizeof(STACK_CANARY));
    } 

    st->capacity   = new_capacity; 
    st->alloc_size = new_bytes;
    *(long long*)st->raw_data             = STACK_CANARY;
    *((long long*)back_canary_ptr(st))    = STACK_CANARY;
    if (st->size > st->capacity) st->size = st->capacity;

    STACK_VERIFY(stack);

    return OK;
}

err_t stack_ctor(stack_id* stack, element_info_t info,
                 const stack_print_fn printer, const stack_sprint_fn sprinter,
                 const stack_info_t stack_info)
{
    if (!CHECK(ERROR, stack != NULL, "stack_ctor: stack_id incorrect")) return ERR_BAD_ARG;
 
    if (!CHECK(ERROR, info.elem_size != 0,    "ctor: elem_size == 0")) return ERR_BAD_ARG;
    if (!CHECK(ERROR, printer        != NULL, "ctor:printer == NULL")) return ERR_BAD_ARG;

    if (info.elem_stride == 0)
    {
        info.elem_stride = round_up(info.elem_size, info.elem_align ? info.elem_align : 1);
    }

    stack_id slot = alloc_slot();
    stack_t* st   = (stack_t*)calloc(1, sizeof(*st));
    if (!CHECK(ERROR, st != NULL, "stack_ctor: st failed to alloc")) return ERR_ALLOC;


    stack_array[slot] = st;
    st->stack_info = stack_info;
    st->elem_info  = info;
    if (st->elem_info.copy_fn == NULL) {
        st->elem_info.copy_fn = stack_memcpy_bytes;
    }

    st->data     = NULL;
    st->size     = 0;
    st->raw_data = NULL; 

    st->printer  = printer;
    st->sprinter = sprinter;

    size_t to_alloc = INITIAL_CAPACITY * st->elem_info.elem_stride + 2 * sizeof(STACK_CANARY);
    void* res       = (void*) calloc(1, to_alloc); 
    if (!CHECK(ERROR, res != NULL, "stack_ctor: res == NULL")) { free_slot(slot); return ERR_BAD_ARG; }
    st->capacity = INITIAL_CAPACITY;
    st->alloc_size  = to_alloc;

    st->raw_data = res;
    st->data     = (void*)((unsigned char*)res + sizeof(STACK_CANARY));

    *((long long*)st->raw_data)        = STACK_CANARY;
    *((long long*)back_canary_ptr(st)) = STACK_CANARY;

    *stack = slot;
    STACK_VERIFY(*stack); 

    return OK;
}

err_t stack_dtor(stack_id stack)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_dtor: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT,
                    "stack_dtor: st == NULL");

    if (st->data)
    {
        free(st->raw_data);
        st->data     = NULL;
        st->raw_data = NULL;
    }
    st->alloc_size = 0;
    st->size       = 0;
    st->capacity   = 0;

    free_slot(stack);
    return OK;
}

size_t stack_size(const stack_id stack)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_size: stack_id incorrect");

    stack_t* st = get_stack(stack);
    
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT,
                    "stack_size: st == NULL");
    
    return st->size;
}

err_t stack_push(stack_id stack, const void* elem)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_push: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT, 
                    "stack_push: st == NULL");
    STACK_ERR_CHECK(ERROR, elem != NULL, stack, ERR_BAD_ARG,
                    "stack_push: elem == NULL");

    STACK_VERIFY(stack);

    err_t err = OK;

    if (st->size == st->capacity){
        size_t target = st->capacity ? st->capacity * 2 : 4;
        err = stack_realloc(stack, target);
        if (err != OK) return err;
    }
    MEM_CPY(calculate_ptr(st, st->size), elem, st->elem_info);
    st->size++;
    
    STACK_VERIFY(stack);
    
    return err;
}

err_t stack_pop (stack_id stack, void* elem)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_pop: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_BAD_ARG, 
                    "stack_pop: st == NULL");
    STACK_ERR_CHECK(ERROR, elem != NULL, stack, ERR_BAD_ARG,
                    "stack_pop: elem == NULL");
    STACK_ERR_CHECK(ERROR, st->size != 0, stack, ERR_CORRUPT,
                    "stack_pop: size == 0");

    STACK_VERIFY(stack);

    st->size--;
    MEM_CPY(elem, calculate_ptr(st, st->size), st->elem_info);

    if (st->capacity >= 8 && st->size <= st->capacity / 4){
        err_t err = stack_realloc(stack, st->capacity / 2);
        if(err != OK) return err;
    }

    STACK_VERIFY(stack);

    return OK;
}

err_t stack_top(stack_id stack, void* elem)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_top: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_BAD_ARG,
                    "stack_top: st == NULL");
    STACK_ERR_CHECK(ERROR, elem != NULL, stack, ERR_BAD_ARG,
                    "stack_top: elem == NULL");
    STACK_ERR_CHECK(ERROR, st->size != 0, stack, ERR_CORRUPT,
                    "stack_top: size == 0");

    STACK_VERIFY(stack);

    MEM_CPY(elem, calculate_ptr(st, st->size - 1), st->elem_info);

    return OK;
}

err_t stack_print(const stack_id stack)
{   
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG,
                    "stack_print: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT, 
               "stack_print: st == NULL");

    STACK_VERIFY(stack);
     
    for (size_t i = 0; i < st->size; i++)
    {
        st->printer(stdout, calculate_ptr(st, i));
    }

    fprintf(stdout, "\n");

    return OK;
}

static void print_hex_bytes(char* res_str, const void* p, const size_t n){
    const unsigned char* b = (const unsigned char*)p;
    size_t pos = strlen(res_str);
    for (size_t i = 0; i < n && pos < STR_CAT_MAX_SIZE; ++i) {
        int written = snprintf(res_str + pos, STR_CAT_MAX_SIZE - pos, 
                               "%02X%s", b[i], (i + 1 < n ? " " : ""));
        if (written < 0) break;
        pos += written;
        if (pos >= STR_CAT_MAX_SIZE) { pos = STR_CAT_MAX_SIZE - 1; break; }
    }
}

err_t stack_dump (logging_level level, const stack_id stack, err_t code, const char* comment)
{
    if (!id_in_range(stack)) { IFLOG(level, "stack_dump: bad id %zu", (size_t)stack); return ERR_BAD_ARG; }
    stack_t* st = get_stack(stack);
    if (!st) { IFLOG(level, "stack_dump: null slot %zu", (size_t)stack); return ERR_BAD_ARG; }

    char res_str[STR_CAT_MAX_SIZE] = {  };

    IFLOG(level, "=== STACK DUMP %p ===", (void*)st);

    snprintf(res_str, STR_CAT_MAX_SIZE, "status        : %s", err_str(code));
    if (comment) {
        size_t len = strlen(res_str);
        snprintf(res_str + len, STR_CAT_MAX_SIZE - len, "  -- %s", comment);
    }

    IFLOG(level, res_str); 

    IFLOG(level, "name          : %s", st->stack_info.name ? st->stack_info.name : "(?)");
    
    IFDEBUG(
        IFLOG(level, "created       : %s:%d in %s",
            st->stack_info.file ? st->stack_info.file : "(?)",
            st->stack_info.line,
            st->stack_info.func ? st->stack_info.func : "(?)");
    )

    IFLOG(level, "type          : %s",   st->elem_info.elem_name ? st->elem_info.elem_name : "(?)");
    IFLOG(level, "alloc size    : %zu",  st->alloc_size);
    IFLOG(level, "set canary    : %lld", (long long)STACK_CANARY);
    IFLOG(level, "canary 1      : %lld", *(long long*)st->raw_data);
    IFLOG(level, "canary 2      : %lld", *(long long*)(back_canary_ptr(st)));
    IFLOG(level, "elem size     : %zu",  st->elem_info.elem_size);
    IFLOG(level, "elem align    : %zu",  st->elem_info.elem_align);
    IFLOG(level, "elem stride   : %zu",  st->elem_info.elem_stride);
    IFLOG(level, "raw data base : %p",   (void*)st->raw_data);
    IFLOG(level, "data base     : %p",   (void*)st->data);

    IFLOG(level, "size/cap      : %zu / %zu", st->size, st->capacity);
    if (st->data == NULL) { IFLOG(level, "(no data) \n === END STACK DUMP ===\n"); return OK;  }

    for (size_t i = 0; i < st->capacity; i++)
    {
        const void* ptr = calculate_ptr(st, i);
        int used = (i < st->size);
        
        snprintf(res_str, STR_CAT_MAX_SIZE, "  [%zu] %s", i, used ? "USED   " : "UNUSED ");

        size_t len2 = strlen(res_str);
        if (st->data) print_hex_bytes(res_str, ptr, st->elem_info.elem_size);
        else          snprintf(res_str + len2, STR_CAT_MAX_SIZE - len2, "(no data)");

        if (used && st->printer){
            size_t len3 = strlen(res_str);
            snprintf(res_str + len3, STR_CAT_MAX_SIZE - len3, "  | value: ");
            st->sprinter(res_str, STR_CAT_MAX_SIZE, ptr);
        }

        IFLOG(level, res_str);
    }

    IFLOG(level, "=== END STACK DUMP ===");

    return OK;
}

err_t stack_verify(const stack_id stack)
{
    STACK_ERR_CHECK(ERROR, id_in_range(stack), stack, ERR_BAD_ARG, 
                    "stack_verify: stack_id incorrect");
    stack_t* st = get_stack(stack);
    STACK_ERR_CHECK(ERROR, st != NULL, stack, ERR_CORRUPT, 
                    "stack_verify: st == NULL");

    const element_info_t* ei = &st->elem_info;
    STACK_CHECK(ERROR, ei != NULL, stack, ERR_CORRUPT, "stack_verify: elem info == NULL");
    STACK_CHECK(ERROR, ei->copy_fn != NULL, stack, ERR_CORRUPT, "stack_verify: copy_fn == NULL");

    STACK_CHECK(ERROR, ei->elem_size != 0, stack, ERR_CORRUPT, "stack_verify: elem size == 0");
    
    STACK_CHECK(ERROR, ei->elem_align != 0, stack, ERR_CORRUPT, "stack_verify: elem align == 0");

    STACK_CHECK(ERROR, ei->elem_stride >= ei->elem_size, stack, ERR_CORRUPT, 
                "stack_verify: elem_stride (%zu) < elem_size (%zu)", ei->elem_stride, ei->elem_size);
    
    STACK_CHECK(ERROR, (ei->elem_stride % ei->elem_align) == 0, stack, ERR_CORRUPT, 
                "stack_verify: elem_stride (%zu) %% elem_align (%zu) != 0", 
                ei->elem_stride, ei->elem_align);
    
    STACK_CHECK(ERROR, st->size <= st->capacity, stack, ERR_CORRUPT, 
                "stack_verify: size (%zu) > capacity (%zu)", st->size, st->capacity);

    if (st->capacity == 0)
    {
        STACK_CHECK(ERROR, st->data == NULL, stack, ERR_CORRUPT, 
                    "stack_verify: capacity == 0 but data != NULL (%p)", (void*)st->data);
    } else {
        STACK_CHECK(ERROR, st->data != NULL, stack, ERR_CORRUPT, 
                    "stack_verify: capacity > 0 but data == NULL");
    }

    STACK_CHECK(ERROR, ((uintptr_t)st->data % ei->elem_align) == 0, stack, ERR_CORRUPT,
                "stack_verify: base pointer %p is not aligned to %zu",
                (void*)st->data, ei->elem_align);

    const unsigned char* base = (const unsigned char*)st->data;
    const uintptr_t      ptr0 = (uintptr_t)(base + 0 * ei->elem_stride);

    STACK_CHECK(ERROR, (ptr0 % ei->elem_align) == 0, stack, ERR_CORRUPT, 
                "stack_verify: ptr[0] misaligned: %p", (void*)ptr0);

    if (st->capacity > 1) {
        const uintptr_t ptr1 = (uintptr_t)(base + 1 * ei->elem_stride);
        STACK_CHECK(ERROR, (ptr1 % ei->elem_align) == 0, stack, ERR_CORRUPT, 
                    "stack_verify: ptr[1] misaligned: %p", (void*)ptr1);
    }

    STACK_CHECK(ERROR, (STACK_CANARY == *(long long*)st->raw_data) && 
                (STACK_CANARY == *(long long*)(back_canary_ptr(st))), stack, ERR_CORRUPT,
                "stack_verify: canary check failed: expected: %lld got: %lld and %lld", 
                 STACK_CANARY, *(long long*)st->raw_data, *((long long*)(back_canary_ptr(st))));

    return OK;
}
