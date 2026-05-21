#include "backend/ir/ir.h"

static err_t ir_touch_vreg_(ir_alloc_t* a, ir_vreg_t v, size_t ip)
{
    if (!ir_vreg_valid(v))
        return OK;

    BE_VEC_GROW(a->intervals, a->interval_cap, v.id + 1, ir_interval_t);

    while (a->interval_count <= v.id)
    {
        a->intervals[a->interval_count] = (ir_interval_t){
            .vreg_id = a->interval_count,
            .first   = SIZE_MAX,
            .last    = 0,
            .type    = IR_TYPE_VOID,
            .loc     = { .kind = IR_LOC_NONE, .preg = -1, .spill_slot = IR_NO_SLOT },
        };

        a->interval_count++;
    }

    ir_interval_t* it = &a->intervals[v.id];

    if (it->first == SIZE_MAX)
        it->first = ip;

    if (it->last < ip)
        it->last = ip;

    it->type = v.type;
    return OK;
}

static err_t ir_collect_intervals_(const ir_func_t* f, ir_alloc_t* a)
{
    if (!f || !a)
        return ERR_BAD_ARG;

    for (size_t ip = 0; ip < f->instr_count; ++ip)
    {
        const ir_instr_t* in = &f->instrs[ip];

        switch (in->op)
        {
            case IR_OP_MOV_IMM_I64:
            case IR_OP_LOAD_SLOT:
            case IR_OP_CALL:
            case IR_OP_MOV_IMM_F64:
            case IR_OP_SCANF_I64:
            case IR_OP_SCANF_F64:
            case IR_OP_GETCHAR_I64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                break;

            case IR_OP_MOV:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_STORE_SLOT:
            case IR_OP_JZ:
            case IR_OP_PRINTF_I64:
            case IR_OP_RET:
            case IR_OP_PRINTF_F64:
            case IR_OP_PUTCHAR_I64:
                if (ir_touch_vreg_(a, in->a, ip) != OK) return ERR_ALLOC;
                break;

            case IR_OP_RUNTIME_SET_PIXEL:
                for (size_t i = 0; i < in->arg_count; ++i)
                    if (ir_touch_vreg_(a, in->args[i], ip) != OK)
                        return ERR_ALLOC;
                break;
            case IR_OP_NEG_I64:
            case IR_OP_NOT_I64:
            case IR_OP_NEG_F64:
            case IR_OP_I64_TO_F64:
            case IR_OP_F64_TO_I64:
            case IR_OP_FLOOR_F64:
            case IR_OP_CEIL_F64:
            case IR_OP_ROUND_F64:
            case IR_OP_SQRT_F64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_ADD_I64:
            case IR_OP_SUB_I64:
            case IR_OP_MUL_I64:
            case IR_OP_DIV_I64:
            case IR_OP_CMP_EQ_I64:
            case IR_OP_CMP_NE_I64:
            case IR_OP_CMP_LT_I64:
            case IR_OP_CMP_GT_I64:
            case IR_OP_CMP_LE_I64:
            case IR_OP_CMP_GE_I64:
            case IR_OP_ADD_F64:
            case IR_OP_SUB_F64:
            case IR_OP_MUL_F64:
            case IR_OP_DIV_F64:
            case IR_OP_CMP_EQ_F64:
            case IR_OP_CMP_NE_F64:
            case IR_OP_CMP_LT_F64:
            case IR_OP_CMP_GT_F64:
            case IR_OP_CMP_LE_F64:
            case IR_OP_CMP_GE_F64:
            case IR_OP_POW_F64:
                case IR_OP_POW_I64:
                if (ir_touch_vreg_(a, in->dst, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->a, ip)   != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->b, ip)   != OK) return ERR_ALLOC;
                break;

            case IR_OP_JCC_FALSE_I64:
            case IR_OP_JCC_FALSE_F64:
                if (ir_touch_vreg_(a, in->a, ip) != OK) return ERR_ALLOC;
                if (ir_touch_vreg_(a, in->b, ip) != OK) return ERR_ALLOC;
                break;

            default:
                break;
        }

        if (in->op == IR_OP_CALL)
        {
            for (size_t i = 0; i < in->arg_count; ++i)
                if (ir_touch_vreg_(a, in->args[i], ip) != OK)
                    return ERR_ALLOC;
        }
    }

    return OK;
}

static int interval_cmp_first_(const void* pa, const void* pb)
{
    const ir_interval_t* a = (const ir_interval_t*)pa;
    const ir_interval_t* b = (const ir_interval_t*)pb;

    if (a->first < b->first) return -1;
    if (a->first > b->first) return  1;
    return 0;
}

static void expire_old_(ir_interval_t** active, size_t* active_count, size_t cur_first, int free_regs[], size_t* free_count)
{
    size_t w = 0;

    for (size_t i = 0; i < *active_count; ++i)
    {
        ir_interval_t* it = active[i];

        if (it->last < cur_first)
            free_regs[(*free_count)++] = it->loc.preg;
        else
            active[w++] = it;
    }

    *active_count = w;
}

static size_t find_active_farthest_(ir_interval_t** active, size_t active_count)
{
    size_t best = 0;

    for (size_t i = 1; i < active_count; ++i)
        if (active[i]->last > active[best]->last)
            best = i;

    return best;
}

err_t ir_alloc_run_linear_scan(const ir_func_t* f, ir_alloc_t* out)
{
    if (!f || !out)
        return ERR_BAD_ARG;

    memset(out, 0, sizeof(*out));

    err_t rc = ir_collect_intervals_(f, out);
    if (rc != OK)
        return rc;

    if (out->interval_count == 0)
        return OK;

    ir_interval_t* sorted = (ir_interval_t*)calloc(out->interval_count, sizeof(*sorted));
    if (!sorted)
        return ERR_ALLOC;

    memcpy(sorted, out->intervals, out->interval_count * sizeof(*sorted));
    qsort(sorted, out->interval_count, sizeof(*sorted), interval_cmp_first_);

    ir_interval_t** active = (ir_interval_t**)calloc(out->interval_count, sizeof(*active));
    if (!active)
    {
        free(sorted);
        return ERR_ALLOC;
    }

    int free_regs[NASM_PREG_COUNT] = { 0 };

    for (int i = 0; i < NASM_PREG_COUNT; ++i)
        free_regs[i] = i;

    size_t free_count = NASM_PREG_COUNT;
    size_t active_count            = 0;

    for (size_t si = 0; si < out->interval_count; ++si)
    {
        ir_interval_t* cur = &out->intervals[sorted[si].vreg_id];

        if (cur->first == SIZE_MAX)
            continue;

        expire_old_(active, &active_count, cur->first, free_regs, &free_count);

        if (free_count > 0)
        {
            cur->loc.kind = IR_LOC_REG;
            cur->loc.preg = free_regs[--free_count];

            active[active_count++] = cur;
            continue;
        }

        size_t far_i = find_active_farthest_(active, active_count);
        ir_interval_t* far = active[far_i];

        if (far->last > cur->last)
        {
            cur->loc = far->loc;

            far->loc.kind = IR_LOC_SPILL;
            far->loc.preg = -1;
            far->loc.spill_slot = out->spill_count++;

            active[far_i] = cur;
        }
        else
        {
            cur->loc.kind = IR_LOC_SPILL;
            cur->loc.preg = -1;
            cur->loc.spill_slot = out->spill_count++;
        }
    }

    out->vreg_loc_count = out->interval_count;
    out->vreg_locs = (ir_alloc_loc_t*)calloc(out->vreg_loc_count, sizeof(*out->vreg_locs));
    if (!out->vreg_locs)
    {
        free(sorted);
        free(active);
        return ERR_ALLOC;
    }

    for (size_t i = 0; i < out->interval_count; ++i)
        out->vreg_locs[i] = out->intervals[i].loc;

    free(sorted);
    free(active);
    return OK;
}
