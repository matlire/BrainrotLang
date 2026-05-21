#include "backend/emitters/nasm_internal.h"

inline static int i64_fits_i32_(i64_t x)
{
    return x >= -2147483647LL - 1LL && x <= 2147483647LL;
}

inline long slot_disp(size_t slot)
{
    return -(long)((slot + 1) * NASM_WORD_BYTES);
}

inline long spill_disp(const ir_func_t* f, size_t spill)
{
    return -(long)((f->slot_count + spill + 1) * NASM_WORD_BYTES);
}

inline void emit_slot(backend_t* be, size_t slot)
{
    be_emitf(be, "[rbp%ld]", slot_disp(slot));
}

inline void emit_spill(backend_t* be, const ir_func_t* f, size_t spill)
{
    be_emitf(be, "[rbp%ld]", spill_disp(f, spill));
}

inline void emit_call_save_slot(backend_t*        be,
                                const ir_func_t*  f,
                                const ir_alloc_t* a,
                                size_t            vreg)
{
    be_emitf(be, "[rbp%ld]",
             slot_disp(f->slot_count + a->spill_count + vreg));
}

void emit_mov_imm_i64_to_vreg(backend_t*        be,
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

    if (i64_fits_i32_(imm))
    {
        be_emitf(be, "    mov  qword ");
        emit_spill(be, f, loc.spill_slot);
        be_emitf(be, ", %lld\n", (long long)imm);
        return;
    }

    be_emitf(be, "    mov  rax, %lld\n", (long long)imm);
    be_emitf(be, "    mov  ");
    emit_spill(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

void emit_mov_imm_f64_to_vreg(backend_t*        be,
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
    emit_spill(be, f, loc.spill_slot);
    be_emitf(be, ", rax\n");
}

#define EMIT_SLOT_VREG(vreg, slot, instr, regs, load)       \
    do                                                      \
    {                                                       \
        ir_alloc_loc_t loc = a->vreg_locs[(vreg).id];       \
        int _load = !!(load);                               \
                                                            \
        if (loc.kind == IR_LOC_REG)                         \
        {                                                   \
            be_emitf(be, "    " instr " ");                 \
            if (_load)                                      \
            {                                               \
                be_emitf(be, "%s, ", (regs)[loc.preg]);     \
                emit_slot(be, slot);                        \
                be_emitf(be, "\n");                         \
            }                                               \
            else                                            \
            {                                               \
                emit_slot(be, slot);                        \
                be_emitf(be, ", %s\n", (regs)[loc.preg]);   \
            }                                               \
            return;                                         \
        }                                                   \
                                                            \
        be_emitf(be, "    mov  rax, ");                     \
        if (_load) emit_slot(be, slot);                     \
        else       emit_spill(be, f, loc.spill_slot);       \
        be_emitf(be, "\n");                                 \
                                                            \
        be_emitf(be, "    mov  ");                          \
        if (_load) emit_spill(be, f, loc.spill_slot);       \
        else       emit_slot(be, slot);                     \
        be_emitf(be, ", rax\n");                            \
    } while (0)

void emit_load_slot_to_vreg_i64(backend_t*        be,
                                const ir_func_t*  f,
                                const ir_alloc_t* a,
                                ir_vreg_t         dst,
                                size_t            slot)
{
    EMIT_SLOT_VREG(dst, slot, "mov ", NASM_PREG64, 1);
}

void emit_store_slot_from_vreg_i64(backend_t*        be,
                                   const ir_func_t*  f,
                                   const ir_alloc_t* a,
                                   ir_vreg_t         src,
                                   size_t            slot)
{
    EMIT_SLOT_VREG(src, slot, "mov ", NASM_PREG64, 0);
}

void emit_load_slot_to_vreg_f64(backend_t*        be,
                                const ir_func_t*  f,
                                const ir_alloc_t* a,
                                ir_vreg_t         dst,
                                size_t            slot)
{
    EMIT_SLOT_VREG(dst, slot, "movsd", NASM_XREG64, 1);
}

void emit_store_slot_from_vreg_f64(backend_t*        be,
                                   const ir_func_t*  f,
                                   const ir_alloc_t* a,
                                   ir_vreg_t         src,
                                   size_t            slot)
{
    EMIT_SLOT_VREG(src, slot, "movsd", NASM_XREG64, 0);
}

#define EMIT_VREG(v, reg_mov, mem_mov, regs, load)          \
    do                                                      \
    {                                                       \
        ir_alloc_loc_t loc = a->vreg_locs[(v).id];          \
        int _load = !!(load);                               \
                                                            \
        if (loc.kind == IR_LOC_REG)                         \
        {                                                   \
            const char* r = (regs)[loc.preg];               \
                                                            \
            if (strcmp(r, scratch) != 0)                    \
            {                                               \
                be_emitf(be, "    " reg_mov " %s, %s\n",    \
                         _load ? scratch : r,               \
                         _load ? r       : scratch);        \
            }                                               \
                                                            \
            return;                                         \
        }                                                   \
                                                            \
        if (_load)                                          \
        {                                                   \
            be_emitf(be, "    " mem_mov " %s, ", scratch);  \
            emit_spill(be, f, loc.spill_slot);              \
            be_emitf(be, "\n");                             \
        }                                                   \
        else                                                \
        {                                                   \
            be_emitf(be, "    " mem_mov " ");               \
            emit_spill(be, f, loc.spill_slot);              \
            be_emitf(be, ", %s\n", scratch);                \
        }                                                   \
    } while (0)

void emit_load_vreg(backend_t*        be,
                    const ir_func_t*  f,
                    const ir_alloc_t* a,
                    ir_vreg_t         v,
                    const char*       scratch)
{
    EMIT_VREG(v, "mov ", "mov ", NASM_PREG64, 1);
}

void emit_store_vreg(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch)
{
    EMIT_VREG(v, "mov ", "mov ", NASM_PREG64, 0);
}

void emit_load_freg(backend_t*        be,
                    const ir_func_t*  f,
                    const ir_alloc_t* a,
                    ir_vreg_t         v,
                    const char*       scratch)
{
    EMIT_VREG(v, "movapd", "movsd", NASM_XREG64, 1);
}

void emit_store_freg(backend_t*        be,
                     const ir_func_t*  f,
                     const ir_alloc_t* a,
                     ir_vreg_t         v,
                     const char*       scratch)
{
    EMIT_VREG(v, "movapd", "movsd", NASM_XREG64, 0);
}

#undef EMIT_SLOT_VREG
#undef EMIT_VREG

int interval_live_across_call(const ir_alloc_t* a,
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
