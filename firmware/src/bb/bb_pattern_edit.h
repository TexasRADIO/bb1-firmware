/* SPDX-License-Identifier: GPL-3.0-only */
/* Editable copy of the playing rich (catalogue) pattern, with Undo. Pure, bounded, main-loop only.
 * - The catalogue is never written: the first edit makes an editable copy, which the caller publishes
 *   through the existing seamless phrase path (bb_audio_phrase_submit_seamless, applied at the loop
 *   wrap) and which scenes already store (BBRP rich-phrase block, bb_rich_scene.c). No new format.
 * - Every edit, kept take or Undo pushes the phrase it replaces, so REC+EDIT walks back to the source.
 * - Step edits reuse the generator's lane writer (bb_gen_lane_write): source order, ordinals and
 *   modifiers of every other event are kept; a cleared cell drops every hit of that voice there.
 * Header-only (static) so the firmware glue and the host test compile the same code without a new
 * link unit. No audio access, allocation, flash or catalogue access. */
#ifndef BB_PATTERN_EDIT_H
#define BB_PATTERN_EDIT_H
#include "bb_gen.h"
#include "bb_live_record.h"
#include "bb_swing.h"
#include "bb_live_capture.h"
#ifndef BB_PATTERN_EDIT_LIBC_PROVIDED   /* the firmware link unit supplies its own libc (libc.c) */
#include <string.h>
#endif
enum { BB_PE_HISTORY = 8 };
enum { BB_PE_OK, BB_PE_INVALID, BB_PE_FULL, BB_PE_NOTHING, BB_PE_CHANGED, BB_PE_UNCHANGED };
enum { BB_PE_ENTRY_PHRASE, BB_PE_ENTRY_NATIVE_COUNT, BB_PE_ENTRY_SONG_COUNT };
typedef struct {
    bb_phrase phrase;
    uint32_t source_ticket,source_epoch;
    uint8_t machine,kind,voice,step,before_count,after_count;
} bb_pe_entry;
typedef struct {
    bb_pe_entry history[BB_PE_HISTORY];   /* oldest first; history[count-1] is the newest */
    uint8_t count;
} bb_pattern_edit;

/* Remember the phrase an edit replaces. A full history forgets its oldest entry. */
static int bb_pe_push(bb_pattern_edit *e, unsigned m, const bb_phrase *before)
{
    if (!e || !before || m > 1u || e->count > BB_PE_HISTORY) return BB_PE_INVALID;
    if (e->count == BB_PE_HISTORY) {
        /* Forward copy, one entry at a time: the target SDK has no memmove (as bb_arrangement.c). */
        for (unsigned i = 0; i + 1u < BB_PE_HISTORY; i++) e->history[i] = e->history[i + 1u];
        e->count--;
    }
    memset(&e->history[e->count],0,sizeof e->history[e->count]);
    e->history[e->count].phrase = *before;
    e->history[e->count].machine = (uint8_t)m;
    e->count++;
    return BB_PE_OK;
}
/* Native counts share the same global depth/order as phrase edits, but Undo
 * restores only the selected count: unrelated native hit/mix edits survive.
 * A producer calls this only after a matching successful owner ACK. */
static int bb_pe_push_count(bb_pattern_edit *e,unsigned m,uint32_t source,uint32_t epoch,
                             unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(!e||m>1u||voice>=BB_VOICES||step>=BB_STEPS||before<1u||before>4u||after<1u||after>4u||e->count>BB_PE_HISTORY)return BB_PE_INVALID;
    if(before==after)return BB_PE_UNCHANGED;
    if(e->count==BB_PE_HISTORY){
        for(unsigned i=0;i+1u<BB_PE_HISTORY;i++)e->history[i]=e->history[i+1u];
        e->count--;
    }
    bb_pe_entry *v=&e->history[e->count++];memset(v,0,sizeof *v);
    v->machine=(uint8_t)m;v->kind=BB_PE_ENTRY_NATIVE_COUNT;v->source_ticket=source;v->source_epoch=epoch;
    v->voice=(uint8_t)voice;v->step=(uint8_t)step;v->before_count=(uint8_t)before;v->after_count=(uint8_t)after;
    return BB_PE_OK;
}
/* Pop only the native edit whose owner Undo was acknowledged. A later main
 * loop reordering or history replacement must never consume another entry. */
static int bb_pe_pop_count_match(bb_pattern_edit *e,unsigned m,uint32_t source,uint32_t epoch,
                                 unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(!e||e->count>BB_PE_HISTORY||m>1u||voice>=BB_VOICES||step>=BB_STEPS||
       before<1u||before>4u||after<1u||after>4u||before==after)return BB_PE_INVALID;
    if(!e->count)return BB_PE_NOTHING;
    const bb_pe_entry *v=&e->history[e->count-1u];
    if(v->machine!=m||v->kind!=BB_PE_ENTRY_NATIVE_COUNT||v->source_ticket!=source||v->source_epoch!=epoch||
       v->voice!=voice||v->step!=step||v->before_count!=before||v->after_count!=after)return BB_PE_CHANGED;
    e->count--;return BB_PE_OK;
}
/* Take back the newest entry: *m and *out are what to publish again. */
static int bb_pe_undo(bb_pattern_edit *e, unsigned *m, bb_phrase *out)
{
    if (!e || !m || !out || e->count > BB_PE_HISTORY) return BB_PE_INVALID;
    if (!e->count) return BB_PE_NOTHING;
    if (e->history[e->count-1u].kind != BB_PE_ENTRY_PHRASE) return BB_PE_CHANGED;
    e->count--;
    *m = e->history[e->count].machine;
    *out = e->history[e->count].phrase;
    return BB_PE_OK;
}
/* Layer m's newest edit was refused by the owner: forget that entry only. Other layers' entries,
 * and their order, stay (a refusal on A must never take B's Undo). */
static void bb_pe_drop_newest(bb_pattern_edit *e, unsigned m)
{
    if (!e || e->count > BB_PE_HISTORY) return;
    for (unsigned r = e->count; r--; )
        if (e->history[r].machine == m) {
            for (unsigned i = r; i + 1u < e->count; i++) e->history[i] = e->history[i + 1u];
            e->count--;
            return;
        }
}
/* Something else replaced layer m's pattern (a new pattern, a scene, a variation): its entries no
 * longer describe what plays, so they go. The other layer's entries stay, in order. */
static void bb_pe_forget(bb_pattern_edit *e, unsigned m)
{
    unsigned w = 0;
    if (!e || e->count > BB_PE_HISTORY) return;
    for (unsigned r = 0; r < e->count; r++)
        if (e->history[r].machine != m) { if (w != r) e->history[w] = e->history[r]; w++; }
    e->count = (uint8_t)w;
}
/* Keep native entries only for their exact active source; phrase-only
 * invalidation must never remove an unrelated native layer's history. */
static void bb_pe_filter_native(bb_pattern_edit *e,unsigned m,int valid,uint32_t ticket,uint32_t epoch){
 if(!e||e->count>BB_PE_HISTORY)return;
 unsigned w=0;
 for(unsigned i=0;i<e->count;i++){
  const bb_pe_entry *v=&e->history[i];
  if(v->machine==m&&v->kind==BB_PE_ENTRY_NATIVE_COUNT&&
     (!valid||v->source_ticket!=ticket||v->source_epoch!=epoch))continue;
  if(w!=i)e->history[w]=*v;w++;
 }
 e->count=(uint8_t)w;
}
static void bb_pe_forget_standalone(bb_pattern_edit *e,unsigned m){
 if(!e||e->count>BB_PE_HISTORY)return;
 unsigned w=0;
 for(unsigned i=0;i<e->count;i++){
  if(e->history[i].machine==m&&e->history[i].kind!=BB_PE_ENTRY_SONG_COUNT)continue;
  if(w!=i)e->history[w]=e->history[i];w++;
 }
 e->count=(uint8_t)w;
}
static void bb_pe_forget_phrases(bb_pattern_edit *e,unsigned m){
 if(!e||e->count>BB_PE_HISTORY)return;
 unsigned w=0;
 for(unsigned i=0;i<e->count;i++){
  if(e->history[i].machine==m&&e->history[i].kind==BB_PE_ENTRY_PHRASE)continue;
  if(w!=i)e->history[w]=e->history[i];w++;
 }
 e->count=(uint8_t)w;
}
static unsigned bb_pe_entries(const bb_pattern_edit *e, unsigned m)
{
    unsigned n = 0;
    if (e && e->count <= BB_PE_HISTORY)
        for (unsigned r = 0; r < e->count; r++) n += e->history[r].machine == m;
    return n;
}
/* Prepare a journalable cell edit without touching the playing owner or source.
 * Every matching source ordinal changes together; accents/flams and event order
 * remain intact. Failure and an unchanged request leave caller scratch intact. */
static int bb_pe_ratchet(const bb_phrase *cur, unsigned voice, unsigned step,
                         unsigned count, bb_phrase *out)
{
    if (!cur || !out || voice >= BB_PHRASE_VOICES || voice == BB_FDV_ACCENT ||
        voice == BB_FDV_ACC || step >= cur->length || count < 1u || count > 4u ||
        !bb_phrase_ratchets_valid(cur)) return BB_PE_INVALID;
    uintptr_t x = (uintptr_t)cur, y = (uintptr_t)out;
    if (x > UINTPTR_MAX - sizeof *cur || y > UINTPTR_MAX - sizeof *out ||
        !(x + sizeof *cur <= y || y + sizeof *out <= x)) return BB_PE_INVALID;
    unsigned matches = 0, changed = 0;
    for (unsigned i = 0; i < cur->count; i++) {
        bb_phrase_event e;
        if (!bb_phrase_read(cur, i, &e)) return BB_PE_INVALID;
        if (e.voice == voice && e.step == step + 1u) {
            matches++;
            changed |= bb_phrase_ratchet_count(cur, i) != count;
        }
    }
    if (!matches) return BB_PE_NOTHING;
    if (!changed) return BB_PE_UNCHANGED;
    *out = *cur;
    (void)bb_phrase_ratchet_set(out, voice, step, count); /* fully preflighted above */
    return BB_PE_OK;
}
/* One STEPS key on a rich pattern: toggle source voice `voice` at zero-based `step`. A new hit is
 * accented when `accent` (REC+step); clearing removes every hit of that voice in the cell. */
static int bb_pe_toggle(const bb_phrase *cur, unsigned voice, unsigned step, unsigned accent, bb_phrase *out)
{
    uint64_t acc = 0, hits, bit;
    if (!cur || !out || cur == out || step >= cur->length || step >= 64u || voice >= BB_PHRASE_VOICES)
        return BB_PE_INVALID;
    hits = bb_gen_lane_hits(cur, voice, &acc);
    bit = UINT64_C(1) << step;
    if (hits & bit) hits &= ~bit;
    else {
        if (cur->count >= BB_FACTORY_DRUM_EVENTS) return BB_PE_FULL;
        hits |= bit;
        acc = accent ? acc | bit : acc & ~bit;
    }
    return bb_gen_lane_write(cur, out, voice, hits, acc, 0u, 0u) ? BB_PE_OK : BB_PE_INVALID;
}
#define BB_PE_HAS_TAKE 1   /* 2.0 live record into the playing pattern (REC+SEQ) */
/* Start a live take over the playing pattern from the audio owner's capture clock: the same grid,
 * rounding and count-in arithmetic as the SONG recorder (bb_record_firmware.inc, BB_RECORD_START).
 * The grid is the pattern's own lattice; with count-in the take opens at the next bar line (*gate). */
static int bb_pe_take_begin(bb_live_record *take, const bb_phrase *playing, const bb_capture_clock *clock,
                            unsigned countin, uint64_t *gate)
{
    if (!take || !playing || !clock || !gate || !clock->applied || !clock->bpm || !clock->sample_rate || !bb_swing_valid(clock->swing))
        return BB_PE_INVALID;
    unsigned subdivision = clock->lattice == BB_FDL_EIGHTHS ? 2 : clock->lattice == BB_FDL_TRIPLETS ? 6 : 4;
    uint32_t even = bb_swing_denominator(clock->sample_rate, clock->swing, 0);
    uint32_t odd = bb_swing_denominator(clock->sample_rate, clock->swing, 1);
    uint32_t divisor = (uint32_t)clock->bpm * subdivision * 100u;
    if (bb_live_record_begin_pair_phase(take, playing, clock->applied, 0, clock->frame, even, odd, divisor,
                                       clock->source_phase, clock->source_step, clock->source_odd))
        return BB_PE_INVALID;
    *gate = clock->frame;
    if (countin) {
        uint64_t wait;
        if (bb_live_record_wait_steps(take, 4u * subdivision - clock->source_step % (4u * subdivision), &wait))
            return BB_PE_INVALID;
        if (wait > UINT64_MAX - *gate) return BB_PE_INVALID;
        *gate += wait;
    }
    return BB_PE_OK;
}
/* KEEP of a live take recorded over the playing pattern. The take must have started from exactly
 * the phrase that plays now; otherwise nothing is published (CHANGED). */
static int bb_pe_take(const bb_live_record *take, uint32_t revision, const bb_phrase *playing, bb_phrase *out)
{
    if (!take || !playing || !out || out == playing) return BB_PE_INVALID;
    if (take->state != BB_REC_REVIEW) return BB_PE_INVALID;
    if (memcmp(&take->before, playing, sizeof *playing)) return BB_PE_CHANGED;
    if (!take->changed) return BB_PE_UNCHANGED;
    return bb_live_record_copy(take, revision, out) ? BB_PE_INVALID : BB_PE_OK;
}
#endif
