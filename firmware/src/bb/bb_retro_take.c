/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_retro_take.h"
#include <string.h>

enum { BB_RETRO_NONE = BB_RETRO_BANKS };

/* This feature sits beside the audio owner but is not part of sound
 * generation.  Keep its bounded bookkeeping out of the hot owner's size
 * budget: the helpers are shared instead of being re-inlined at each gate,
 * and the JieLi compiler may optimise them for minimum size. */
#define BB_RETRO_SMALL __attribute__((minsize,noinline))

static BB_RETRO_SMALL int apart(const void *a, size_t n, const void *b, size_t m) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return a && b && x <= UINTPTR_MAX - n && y <= UINTPTR_MAX - m &&
           (x + n <= y || y + m <= x);
}

static BB_RETRO_SMALL int owner_valid(const bb_retro_owner *o) {
    return o && o->target < 2u && o->length && o->length <= 64u &&
           o->lattice <= 2u && o->sample_rate && o->bpm && o->swing <= 100u &&
           o->loop;
}

static BB_RETRO_SMALL int same_source(const bb_retro_owner *a, const bb_retro_owner *b) {
    return a->source_epoch == b->source_epoch && a->applied == b->applied &&
           a->transport_epoch == b->transport_epoch &&
           a->sample_rate == b->sample_rate && a->bpm == b->bpm &&
           a->target == b->target && a->kit == b->kit &&
           a->lattice == b->lattice && a->length == b->length &&
           a->swing == b->swing;
}

static BB_RETRO_SMALL int same_loop(const bb_retro_owner *a, const bb_retro_owner *b) {
    return same_source(a, b) && a->loop == b->loop;
}

static BB_RETRO_SMALL void bank_start(bb_retro_bank *b, const bb_retro_owner *o) {
    memset(b, 0, sizeof *b);
    b->owner = *o;
    b->valid = 1;
}

BB_RETRO_SMALL void bb_retro_take_reset(bb_retro_take *r) {
    if (!r) return;
    memset(r, 0, sizeof *r);
    r->ready = r->claimed = BB_RETRO_NONE;
}

static BB_RETRO_SMALL void unpublished_clear(bb_retro_take *r) {
    if (r->ready < BB_RETRO_BANKS && r->ready != r->claimed)
        memset(&r->bank[r->ready], 0, sizeof r->bank[r->ready]);
    r->ready = BB_RETRO_NONE;
}

static BB_RETRO_SMALL int restart_changed(bb_retro_take *r, const bb_retro_owner *o) {
    unpublished_clear(r);
    if (r->current >= BB_RETRO_BANKS || r->current == r->claimed)
        r->current = (uint8_t)(r->claimed == 0u ? 1u : 0u);
    bank_start(&r->bank[r->current], o);
    r->active = 1;
    return BB_RETRO_CHANGED;
}

BB_RETRO_SMALL int bb_retro_take_observe(bb_retro_take *r, const bb_retro_owner *o) {
    if (!r || !owner_valid(o) || r->ready > BB_RETRO_NONE ||
        r->claimed > BB_RETRO_NONE || r->current >= BB_RETRO_BANKS ||
        r->ready == r->current || r->claimed == r->current)
        return BB_RETRO_INVALID;

    if (!r->active) {
        bank_start(&r->bank[r->current], o);
        r->active = 1;
        return BB_RETRO_EMPTY;
    }

    bb_retro_bank *current = &r->bank[r->current];
    if (!current->valid)
        return restart_changed(r, o);
    if (!same_source(&current->owner, o))
        return restart_changed(r, o);
    if (o->loop == current->owner.loop)
        return BB_RETRO_EMPTY;
    if (o->loop != current->owner.loop + 1u)
        return restart_changed(r, o);

    int result = BB_RETRO_OK;
    if (current->overflow) {
        result = BB_RETRO_OVERFLOW;
    } else if (r->claimed < BB_RETRO_BANKS) {
        /* Keep the claimed loop immutable.  Reuse the current bank for the
         * next pass and make the missed publication visible. */
        r->refused++;
        result = BB_RETRO_BUSY;
    } else {
        uint8_t next = (uint8_t)(r->current ^ 1u);
        if (r->ready < BB_RETRO_BANKS) r->overwritten++;
        r->ready = r->current;
        r->current = next;
        current = &r->bank[r->current];
    }
    bank_start(current, o);
    return result;
}

BB_RETRO_SMALL int bb_retro_take_cell(const bb_retro_owner *o,unsigned step,uint32_t phase,
                       uint32_t denominator,unsigned voice,unsigned accented,uint16_t *out) {
    if(!owner_valid(o)||!out||step>=o->length||!denominator||phase>=denominator||
       voice>=32u||accented>1u)return BB_RETRO_INVALID;
    unsigned cell=(step+(phase>=denominator/2u+denominator%2u))%o->length;
    *out=(uint16_t)(voice|(cell<<5)|((accented?2u:1u)<<11));
    return BB_RETRO_OK;
}

BB_RETRO_SMALL int bb_retro_take_hit(bb_retro_take *r, const bb_retro_owner *o,
                      uint16_t event) {
    if (!r || !owner_valid(o) || !r->active || (event>>13) ||
        r->current >= BB_RETRO_BANKS)
        return BB_RETRO_INVALID;
    bb_retro_bank *b = &r->bank[r->current];
    unsigned cell=(event>>5)&63u,intensity=(event>>11)&3u;
    if (!b->valid || !same_loop(&b->owner, o) || cell>=o->length ||
        !intensity)
        return BB_RETRO_CHANGED;
    if (b->overflow) return BB_RETRO_OVERFLOW;
    if (b->count == BB_RETRO_CAPACITY) {
        b->overflow = 1;
        return BB_RETRO_OVERFLOW;
    }
    b->event[b->count++] = event;
    return BB_RETRO_OK;
}

#if !defined(BB_RETRO_PRODUCTION)
BB_RETRO_SMALL int bb_retro_take_claim(bb_retro_take *r, bb_retro_snapshot *out) {
    if (!apart(r, sizeof *r, out, sizeof *out) ||
        r->ready > BB_RETRO_NONE || r->claimed > BB_RETRO_NONE)
        return BB_RETRO_INVALID;
    if (r->claimed < BB_RETRO_BANKS) return BB_RETRO_BUSY;
    if (r->ready == BB_RETRO_NONE) return BB_RETRO_EMPTY;
    bb_retro_bank *b = &r->bank[r->ready];
    if (!b->valid || b->overflow)
        return BB_RETRO_INVALID;
    r->claimed = r->ready;
    r->ready = BB_RETRO_NONE;
    r->cursor = 0;
    *out = (bb_retro_snapshot){b->owner, b->count};
    return BB_RETRO_OK;
}

BB_RETRO_SMALL int bb_retro_take_read(bb_retro_take *r, uint16_t *out) {
    if (!apart(r, sizeof *r, out, sizeof *out) ||
        r->claimed > BB_RETRO_NONE)
        return BB_RETRO_INVALID;
    if (r->claimed == BB_RETRO_NONE) return BB_RETRO_EMPTY;
    bb_retro_bank *b = &r->bank[r->claimed];
    if (!b->valid || b->overflow || r->cursor > b->count)
        return BB_RETRO_INVALID;
    if (r->cursor == b->count) return BB_RETRO_CLOSED;
    *out = b->event[r->cursor++];
    return BB_RETRO_OK;
}
BB_RETRO_SMALL int bb_retro_take_release(bb_retro_take *r) {
    if (!r || r->claimed > BB_RETRO_NONE) return BB_RETRO_INVALID;
    if (r->claimed == BB_RETRO_NONE) return BB_RETRO_EMPTY;
    memset(&r->bank[r->claimed], 0, sizeof r->bank[r->claimed]);
    r->claimed = BB_RETRO_NONE;
    r->cursor = 0;
    return BB_RETRO_OK;
}

BB_RETRO_SMALL size_t bb_retro_take_ram_bytes(void) { return sizeof(bb_retro_take); }
#endif

#undef BB_RETRO_SMALL
