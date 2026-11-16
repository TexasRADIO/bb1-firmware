/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A: the core-1 job that renders kit A's metal lanes for one call (TIMING-OPTIONS-R9.9.md, #4).
 * Its own 64-byte-aligned object, NOT inside bb_c1_private_pool: the retained pool and its mailbox layout stay as
 * they are. Same ownership rules as bb_c1_drum_job: CPU0 prepares only when no job is outstanding; the worker runs
 * once per accepted ticket and writes only this object; CPU0 commits only after completion or a confirmed hold, and
 * validates everything before any live write. Checks are FNV-1a over every input and output byte; they detect
 * corruption, they are not authentication. Not yet published by audio_span (wiring is the next step). */
#ifndef BB_C1_METAL_JOB_H
#define BB_C1_METAL_JOB_H
#include <stdint.h>
#include <stddef.h>
#include "bb_c1_drum_job.h"
#include "x0x/metal_capture.h"
#define BB_C1_METAL_GUARD UINT32_C(0xD8C1A3E7)
typedef struct {
    uint32_t guard, ticket, input_check;
    uint16_t span_frames[BB_METAL_SPANS];
    uint8_t nspans, pad[3];
    bb_metal_state state;      /* kit A's metal state at the call start; after run, at the call end */
    gg_state groups;           /* kit A's group faders at the call start; after run, at the call end */
    bb_metal_hits hits;        /* kit A's metal hits in the call, in firmware order */
    bb_metal_terms terms;      /* run's output: per-lane values for CPU0's merge */
    int32_t result;            /* capture_call's return: 0 nothing to add, 1 values */
    uint32_t end_guard;
} bb_c1_metal_job;
_Static_assert(sizeof(bb_c1_metal_job) <= 4096u, "fix A job stays within one 4 KiB budget");
_Static_assert(offsetof(bb_c1_metal_job, state) % 4u == 0u && offsetof(bb_c1_metal_job, terms) % 4u == 0u, "word-aligned");

static uint32_t bb_c1_metal_input_check(const bb_c1_metal_job *j) {
    uint32_t h = UINT32_C(2166136261);
    h = bb_c1_drum_hash(h, &j->ticket, sizeof j->ticket);
    h = bb_c1_drum_hash(h, j->span_frames, sizeof j->span_frames);
    h = bb_c1_drum_hash(h, &j->nspans, sizeof j->nspans);
    h = bb_c1_drum_hash(h, &j->state, sizeof j->state);
    h = bb_c1_drum_hash(h, &j->groups, sizeof j->groups);
    h = bb_c1_drum_hash(h, &j->hits, sizeof j->hits);
    return h;
}
static uint32_t bb_c1_metal_output_check(const bb_c1_metal_job *j) {
    uint32_t h = bb_c1_metal_input_check(j);
    h = bb_c1_drum_hash(h, &j->terms, sizeof j->terms);
    h = bb_c1_drum_hash(h, &j->result, sizeof j->result);
    return h;
}
static int bb_c1_metal_valid(const bb_c1_metal_job *j, uint32_t ticket) {
    return j && ticket && j->ticket == ticket && j->guard == BB_C1_METAL_GUARD && j->end_guard == BB_C1_METAL_GUARD &&
           j->nspans && j->nspans <= BB_METAL_SPANS && j->hits.n <= BB_METAL_HITS;
}
/* CPU0, no job outstanding: kit A's metal state, faders, the call's spans and metal hits. 0 = not representable
 * (the caller renders kit A's metal on CPU0 as today). */
static int bb_c1_metal_prepare(bb_c1_metal_job *j, const drum808_t *kit_a, const gg_state *groups_a,
                               const uint16_t *span_frames, unsigned nspans, const bb_metal_hits *hits, uint32_t ticket) {
    unsigned total = 0;
    if (!j || !kit_a || !groups_a || !span_frames || !hits || !ticket || !nspans || nspans > BB_METAL_SPANS ||
        hits->n > BB_METAL_HITS || !bb_group_state_valid(groups_a)) return 0;
    for (unsigned s = 0; s < nspans; s++) { if (!span_frames[s]) return 0; total += span_frames[s]; }
    if (total > BB_METAL_MAX) return 0;
    __builtin_memset(j, 0, sizeof *j);
    j->guard = j->end_guard = BB_C1_METAL_GUARD; j->ticket = ticket; j->nspans = (uint8_t)nspans;
    for (unsigned s = 0; s < nspans; s++) j->span_frames[s] = span_frames[s];
    bb_metal_extract(kit_a, &j->state);
    bb_c1_drum_copy(&j->groups, groups_a, sizeof j->groups);
    bb_c1_drum_copy(&j->hits, hits, sizeof j->hits);
    j->input_check = bb_c1_metal_input_check(j);
    return 1;
}
/* Worker: once per accepted ticket, on this object only. */
static int bb_c1_metal_run(bb_c1_metal_job *j, uint32_t ticket, uint32_t *result) {
    if (!result || !bb_c1_metal_valid(j, ticket) || j->input_check != bb_c1_metal_input_check(j) ||
        !bb_group_state_valid(&j->groups)) return 0;
    j->result = bb_metal_capture_call(&j->state, &j->groups, &j->terms, j->span_frames, j->nspans, &j->hits);
    if (j->result < 0) return 0;
    *result = bb_c1_metal_output_check(j);
    return 1;
}
/* CPU0 after completion or a confirmed hold: validate everything, then merge kit A's metal into the buses (after
 * CPU0's own lanes, as the renderer orders them) and write back the state and faders. 0 = nothing written. */
static int bb_c1_metal_commit(bb_c1_metal_job *j, uint32_t ticket, uint32_t result, drum808_t *kit_a, gg_state *groups_a,
                              float *dry, float *rev, float *dly) {
    if (!kit_a || !groups_a || !dry || !rev || !dly || !bb_c1_metal_valid(j, ticket) ||
        bb_c1_metal_output_check(j) != result || j->result < 0 || j->result > 1 || !bb_group_state_valid(&j->groups))
        return 0;
    if (j->result == 1)
        for (unsigned k = 0; k < BB_METAL_LANES; k++)
            for (unsigned i = 0; i < j->terms.frames; i++) {
                uint32_t bits; __builtin_memcpy(&bits, &j->terms.v[k][i], sizeof bits);
                if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) return 0;
            }
    if (j->result == 1) bb_metal_merge(&j->terms, dry, rev, dly);
    bb_metal_apply(kit_a, &j->state);
    bb_c1_drum_copy(groups_a, &j->groups, sizeof *groups_a);
    return 1;
}
#endif
