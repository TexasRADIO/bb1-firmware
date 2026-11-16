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
#include "bb_retro_take.h"
#ifndef BB_WITH_PHRASE_NUDGE
#define BB_WITH_PHRASE_NUDGE 0
#endif
#if BB_WITH_PHRASE_NUDGE
#include "bb_phrase_nudge_gesture.h"
#endif
#ifndef BB_PATTERN_EDIT_LIBC_PROVIDED   /* the firmware link unit supplies its own libc (libc.c) */
#include <string.h>
#endif
enum { BB_PE_HISTORY = 8 };
enum { BB_PE_OK, BB_PE_INVALID, BB_PE_FULL, BB_PE_NOTHING, BB_PE_CHANGED, BB_PE_UNCHANGED };
enum { BB_PE_ENTRY_PHRASE, BB_PE_ENTRY_NATIVE_COUNT, BB_PE_ENTRY_SONG_COUNT, BB_PE_ENTRY_NATIVE_STEP
#if BB_WITH_PHRASE_NUDGE
       , BB_PE_ENTRY_NUDGE
#endif
};
typedef struct {
#if BB_WITH_PHRASE_NUDGE
    /* Phrase Undo must restore its timing sidecar with the notes.  Sharing the
     * combined phrase snapshot with the smaller one-cell transaction keeps
     * one global journal and makes the RAM cost explicit: 34 bytes of state
     * plus target/host alignment (36 bytes measured per entry), rather than
     * silently dropping timing on an ordinary phrase Undo. */
    union {
        struct { bb_phrase phrase; bb_phrase_nudges phrase_nudges; };
        bb_phrase_nudge_transaction nudge;
        struct {
            uint32_t source_ticket,source_epoch;
            uint8_t voice,step,before_count,after_count;
        } native;
    };
#else
    union {
        bb_phrase phrase;
        struct {
            uint32_t source_ticket,source_epoch;
            uint8_t voice,step,before_count,after_count;
        } native;
    };
#endif
    uint8_t machine,kind;
} bb_pe_entry;
#if BB_WITH_PHRASE_NUDGE
_Static_assert(sizeof(bb_phrase)+sizeof(bb_phrase_nudges)>=sizeof(bb_phrase_nudge_transaction),
               "nudge transaction must reuse combined phrase journal storage");
#endif
typedef struct {
    bb_pe_entry history[BB_PE_HISTORY];   /* oldest first; history[count-1] is the newest */
    uint8_t count;
} bb_pattern_edit;

/* Remember the phrase an edit replaces. A full history forgets its oldest entry. */
#if BB_WITH_PHRASE_NUDGE
static int bb_pe_push_state(bb_pattern_edit *e,unsigned m,const bb_phrase *before,
                            const bb_phrase_nudges *nudges)
{
    if(!e||!before||!nudges||m>1u||e->count>BB_PE_HISTORY||
       !bb_phrase_nudges_valid(before,nudges))return BB_PE_INVALID;
    if(e->count==BB_PE_HISTORY){
        for(unsigned i=0;i+1u<BB_PE_HISTORY;i++)e->history[i]=e->history[i+1u];
        e->count--;
    }
    bb_pe_entry *v=&e->history[e->count++];memset(v,0,sizeof *v);
    v->phrase=*before;v->phrase_nudges=*nudges;v->machine=(uint8_t)m;
    return BB_PE_OK;
}
#endif
static int bb_pe_push(bb_pattern_edit *e, unsigned m, const bb_phrase *before)
{
#if BB_WITH_PHRASE_NUDGE
    const bb_phrase_nudges empty={0};
    return bb_pe_push_state(e,m,before,&empty);
#else
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
#endif
}
/* Native counts share the same global depth/order as phrase edits, but Undo
 * restores only the selected count: unrelated native hit/mix edits survive.
 * A producer calls this only after a matching successful owner ACK. */
static int bb_pe_push_native(bb_pattern_edit *e,unsigned kind,unsigned m,uint32_t source,uint32_t epoch,
                             unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(!e||m>1u||voice>=BB_VOICES||step>=BB_STEPS||e->count>BB_PE_HISTORY)return BB_PE_INVALID;
    if(before==after)return BB_PE_UNCHANGED;
    if(e->count==BB_PE_HISTORY){
        for(unsigned i=0;i+1u<BB_PE_HISTORY;i++)e->history[i]=e->history[i+1u];
        e->count--;
    }
    bb_pe_entry *v=&e->history[e->count++];memset(v,0,sizeof *v);
    v->machine=(uint8_t)m;v->kind=(uint8_t)kind;v->native.source_ticket=source;v->native.source_epoch=epoch;
    v->native.voice=(uint8_t)voice;v->native.step=(uint8_t)step;
    v->native.before_count=(uint8_t)before;v->native.after_count=(uint8_t)after;
    return BB_PE_OK;
}
static int bb_pe_push_count(bb_pattern_edit *e,unsigned m,uint32_t source,uint32_t epoch,
                            unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(before<1u||before>4u||after<1u||after>4u)return BB_PE_INVALID;
    return bb_pe_push_native(e,BB_PE_ENTRY_NATIVE_COUNT,m,source,epoch,voice,step,before,after);
}
/* Pop only the native edit whose owner Undo was acknowledged. A later main
 * loop reordering or history replacement must never consume another entry. */
static int bb_pe_pop_native_match(bb_pattern_edit *e,unsigned kind,unsigned m,uint32_t source,uint32_t epoch,
                                 unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(!e||e->count>BB_PE_HISTORY||m>1u||voice>=BB_VOICES||step>=BB_STEPS||
       before==after)return BB_PE_INVALID;
    if(!e->count)return BB_PE_NOTHING;
    const bb_pe_entry *v=&e->history[e->count-1u];
    if(v->machine!=m||v->kind!=kind||v->native.source_ticket!=source||v->native.source_epoch!=epoch||
       v->native.voice!=voice||v->native.step!=step||v->native.before_count!=before||
       v->native.after_count!=after)return BB_PE_CHANGED;
    e->count--;return BB_PE_OK;
}
static int bb_pe_pop_count_match(bb_pattern_edit *e,unsigned m,uint32_t source,uint32_t epoch,
                                 unsigned voice,unsigned step,unsigned before,unsigned after)
{
    if(before<1u||before>4u||after<1u||after>4u)return BB_PE_INVALID;
    return bb_pe_pop_native_match(e,BB_PE_ENTRY_NATIVE_COUNT,m,source,epoch,voice,step,before,after);
}
#if BB_WITH_PHRASE_NUDGE
/* Journal one already-acknowledged gesture. The supplied transaction must be
 * an exact one-cell delta; malformed callers cannot smuggle a second edit into
 * the same Undo operation. History admission follows the same global depth
 * and oldest-first ageing as phrase and native-count edits. */
static int bb_pe_push_nudge(bb_pattern_edit *e,const bb_phrase *p,
                            const bb_phrase_nudge_transaction *tx)
{
    if(!e||!p||!tx||e->count>BB_PE_HISTORY||tx->machine>1u||
       tx->voice>=BB_PHRASE_VOICES||tx->step>=p->length||!tx->source_ticket||
       !bb_phrase_nudges_valid(p,&tx->before)||
       !bb_phrase_nudges_valid(p,&tx->after)||
       !memcmp(&tx->before,&tx->after,sizeof tx->before))return BB_PE_INVALID;
    int value;bb_phrase_nudges exact=tx->before;
    if(!bb_phrase_nudge_get(p,&tx->after,tx->voice,tx->step,&value)||
       !bb_phrase_nudge_set(p,&exact,tx->voice,tx->step,value)||
       memcmp(&exact,&tx->after,sizeof exact))return BB_PE_INVALID;
    if(e->count==BB_PE_HISTORY){
        for(unsigned i=0;i+1u<BB_PE_HISTORY;i++)e->history[i]=e->history[i+1u];
        e->count--;
    }
    bb_pe_entry *v=&e->history[e->count++];memset(v,0,sizeof *v);
    v->nudge=*tx;v->machine=tx->machine;v->kind=BB_PE_ENTRY_NUDGE;return BB_PE_OK;
}
/* Undo is journal-first but owner-acknowledged: peek does not mutate history.
 * The caller submits `before` only if the active owner still equals `after`,
 * then removes this exact newest token after the matching owner ACK. */
static int bb_pe_nudge_latest(const bb_pattern_edit *e,bb_phrase_nudge_transaction *out)
{
    if(!e||!out||e->count>BB_PE_HISTORY)return BB_PE_INVALID;
    if(!e->count)return BB_PE_NOTHING;
    const bb_pe_entry *v=&e->history[e->count-1u];
    if(v->kind!=BB_PE_ENTRY_NUDGE)return BB_PE_CHANGED;
    *out=v->nudge;return BB_PE_OK;
}
static int bb_pe_pop_nudge_match(bb_pattern_edit *e,const bb_phrase_nudge_transaction *tx)
{
    if(!e||!tx||e->count>BB_PE_HISTORY)return BB_PE_INVALID;
    if(!e->count)return BB_PE_NOTHING;
    const bb_pe_entry *v=&e->history[e->count-1u];
    if(v->kind!=BB_PE_ENTRY_NUDGE||memcmp(&v->nudge,tx,sizeof *tx))return BB_PE_CHANGED;
    e->count--;return BB_PE_OK;
}
#endif
/* Take back the newest entry: *m and *out are what to publish again. */
#if BB_WITH_PHRASE_NUDGE
static int bb_pe_undo_state(bb_pattern_edit *e,unsigned *m,bb_phrase *out,
                            bb_phrase_nudges *nudges)
{
    if(!e||!m||!out||!nudges||e->count>BB_PE_HISTORY)return BB_PE_INVALID;
    if(!e->count)return BB_PE_NOTHING;
    if(e->history[e->count-1u].kind!=BB_PE_ENTRY_PHRASE)return BB_PE_CHANGED;
    const bb_pe_entry *v=&e->history[e->count-1u];
    if(!bb_phrase_nudges_valid(&v->phrase,&v->phrase_nudges))return BB_PE_INVALID;
    e->count--;*m=v->machine;*out=v->phrase;*nudges=v->phrase_nudges;return BB_PE_OK;
}
#endif
static int bb_pe_undo(bb_pattern_edit *e, unsigned *m, bb_phrase *out)
{
#if BB_WITH_PHRASE_NUDGE
    bb_phrase_nudges ignored;
    return bb_pe_undo_state(e,m,out,&ignored);
#else
    if (!e || !m || !out || e->count > BB_PE_HISTORY) return BB_PE_INVALID;
    if (!e->count) return BB_PE_NOTHING;
    if (e->history[e->count-1u].kind != BB_PE_ENTRY_PHRASE) return BB_PE_CHANGED;
    e->count--;
    *m = e->history[e->count].machine;
    *out = e->history[e->count].phrase;
    return BB_PE_OK;
#endif
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
  if(v->machine==m&&(v->kind==BB_PE_ENTRY_NATIVE_COUNT||v->kind==BB_PE_ENTRY_NATIVE_STEP)&&
     (!valid||v->native.source_ticket!=ticket||v->native.source_epoch!=epoch))continue;
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
  if(e->history[i].machine==m&&(e->history[i].kind==BB_PE_ENTRY_PHRASE
#if BB_WITH_PHRASE_NUDGE
     ||e->history[i].kind==BB_PE_ENTRY_NUDGE
#endif
     ))continue;
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
#if BB_WITH_RETRO_TAKE
enum { BB_PE_RETRO_EMPTY, BB_PE_RETRO_PREPARED, BB_PE_RETRO_SUBMITTED, BB_PE_RETRO_COMMITTED };
typedef struct {
    bb_phrase before, after;
    bb_retro_owner owner;
    uint32_t destination_revision, ticket;
    uint8_t target, state;
} bb_pe_retro_transaction;

/* Prepare a claimed completed loop for one explicit playing-pattern revision.
 * This is main-loop-only and pure: it neither publishes the phrase nor mutates
 * Undo.  The caller must submit `after`, bind the returned owner ticket with
 * bb_pe_retro_submitted(), and journal only after the exact owner ACK. */
static int bb_pe_retro_owner_matches(const bb_retro_owner *take,const bb_retro_owner *now)
{
    return take && now && take->source_epoch == now->source_epoch &&
        take->applied == now->applied && take->transport_epoch == now->transport_epoch &&
        take->sample_rate == now->sample_rate && take->bpm == now->bpm &&
        take->target == now->target && take->kit == now->kit &&
        take->lattice == now->lattice && take->length == now->length &&
        take->swing == now->swing && now->loop > take->loop;
}
/* Merge audio-owned, already quantised cells without constructing the much
 * larger prospective-recorder transaction.  Existing source events stay
 * byte-exact; only a hit first created by this TAKE may be accent-upgraded by
 * a later strike at the same cell. */
static int bb_pe_retro_merge(const bb_phrase *playing,const uint16_t *events,
    unsigned count,bb_phrase *after)
{
    if(!playing||(!events&&count)||!after||after==playing||count>BB_RETRO_CAPACITY||
       !playing->length||playing->length>BB_STEPS||playing->count>BB_FACTORY_DRUM_EVENTS||
       playing->lattice>BB_FDL_TRIPLETS||!bb_phrase_ratchets_valid(playing))return BB_PE_INVALID;
    *after=*playing;unsigned before=playing->count,changed=0;
    for(unsigned i=0;i<count;i++){
        unsigned event=events[i],voice=event&31u,cell=(event>>5)&63u,
            intensity=(event>>11)&3u,j;
        if((event>>13)||!intensity||intensity>2u||cell>=after->length)return BB_PE_CHANGED;
        if(voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC)return BB_PE_INVALID;
        for(j=0;j<after->count;j++){
            unsigned old=after->events[j];
            if((old&31u)!=voice||((old>>5)&63u)!=cell)continue;
            if(intensity==2u&&j>=before&&((old>>11)&3u)!=BB_FDI_ACCENTED){
                after->events[j]=(uint16_t)((old&~(3u<<11))|(BB_FDI_ACCENTED<<11));changed=1;
            }
            break;
        }
        if(j<after->count)continue;
        if(after->count==BB_FACTORY_DRUM_EVENTS)return BB_PE_FULL;
        after->events[after->count++]=(uint16_t)(voice|(cell<<5)|(intensity<<11));changed=1;
    }
    return changed?BB_PE_OK:BB_PE_UNCHANGED;
}
static int bb_pe_retro_prepare(bb_pe_retro_transaction *transaction,
    const bb_retro_snapshot *snapshot,const uint16_t *events,unsigned count,
    const bb_retro_owner *current,uint32_t destination_revision,const bb_phrase *playing)
{
    if(!transaction||!snapshot||(!events&&count)||!current||!playing||
       transaction->state!=BB_PE_RETRO_EMPTY||destination_revision==UINT32_MAX||
       count!=snapshot->count||count>BB_RETRO_CAPACITY||
       !bb_pe_retro_owner_matches(&snapshot->owner,current)||
       playing->length!=snapshot->owner.length||playing->lattice!=snapshot->owner.lattice)
        return BB_PE_INVALID;
    bb_phrase after;int result=bb_pe_retro_merge(playing,events,count,&after);
    if(result!=BB_PE_OK)return result;
    bb_pe_retro_transaction ready={.before=*playing,.after=after,.owner=snapshot->owner,
        .destination_revision=destination_revision,.target=snapshot->owner.target,
        .state=BB_PE_RETRO_PREPARED};
    *transaction=ready;return BB_PE_OK;
}
static int bb_pe_retro_submitted(bb_pe_retro_transaction *transaction,uint32_t ticket)
{
    if(!transaction||transaction->state!=BB_PE_RETRO_PREPARED||!ticket)return BB_PE_INVALID;
    transaction->ticket=ticket;transaction->state=BB_PE_RETRO_SUBMITTED;return BB_PE_OK;
}
/* Journal only the exact acknowledged destination.  Refusal, a stale ticket,
 * a different revision or different playing bytes leave history untouched. */
static int bb_pe_retro_commit(bb_pattern_edit *history,bb_pe_retro_transaction *transaction,
    uint32_t acknowledged_ticket,uint32_t current_revision,const bb_phrase *playing)
{
    if(!history||!transaction||!playing||transaction->state!=BB_PE_RETRO_SUBMITTED)
        return BB_PE_INVALID;
    if(!acknowledged_ticket||acknowledged_ticket!=transaction->ticket||
       transaction->destination_revision==UINT32_MAX||
       current_revision!=transaction->destination_revision+1u||
       memcmp(playing,&transaction->after,sizeof *playing))return BB_PE_CHANGED;
    int result=bb_pe_push(history,transaction->target,&transaction->before);
    if(result==BB_PE_OK)transaction->state=BB_PE_RETRO_COMMITTED;
    return result;
}
static int bb_pe_retro_cancel(bb_pe_retro_transaction *transaction)
{
    if(!transaction||transaction->state==BB_PE_RETRO_EMPTY||
       transaction->state==BB_PE_RETRO_COMMITTED)return BB_PE_INVALID;
    memset(transaction,0,sizeof *transaction);return BB_PE_OK;
}
#endif
#endif
