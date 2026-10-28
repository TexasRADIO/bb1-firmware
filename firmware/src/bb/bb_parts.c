#include "bb_wire.h"
#include "bb_parts.h"
#include "bb_session.h"
#include <string.h>

static int phrase_valid(unsigned steps) {
    return steps >= 16u && steps <= BB_STEPS && steps % 16u == 0u;
}
static int families_valid(const uint8_t *f) {
    if(!f)return 0;
    for(unsigned i=0;i<BB_MACHINES;i++)if(f[i]>1)return 0;
    return 1;
}
static int families_match(const bb_parts *p,unsigned slot) {
    return families_valid(p->family[slot]) &&
        !memcmp(p->family[slot],p->current_family,BB_MACHINES);
}
static int ready(const bb_parts *p) {
    return p && p->ready == 1 && families_valid(p->current_family) && p->active < BB_PART_COUNT &&
        phrase_valid(p->phrase_steps[p->active]) &&
        p->position < p->phrase_steps[p->active];
}
static void capture(bb_core *dst, const bb_core *src) {
    *dst = *src;
    dst->frame = 0; dst->phase = 0; dst->ticks = 0;
    dst->running = 0; dst->pending_tick = 0; dst->swing_odd = 0;
    dst->active_swing = dst->swing;
    dst->selected_machine = 0; dst->selected_voice = 0;
    dst->page = BB_DRUMS; dst->edit = BB_GATE;
    for (unsigned i = 0; i < BB_MACHINES; i++) {
        dst->drums[i].next = 0; dst->drums[i].sound_revision = 1;
    }
    for (unsigned i = 0; i < BB_SYNTHS; i++) dst->synths[i].next = 0;
}
int bb_parts_init_families(bb_parts *p,const bb_core *core,unsigned steps,const uint8_t family[BB_MACHINES]) {
    if(!p || !phrase_valid(steps) || !bb_session_state_valid(core) || !families_valid(family))return 0;
    uint8_t tags[BB_MACHINES];memcpy(tags,family,sizeof tags);
    memset(p, 0, sizeof *p);
    for (unsigned i = 0; i < BB_PART_COUNT; i++) {
        capture(&p->scenes[i], core); p->phrase_steps[i] = (uint8_t)steps;
        memcpy(p->family[i],tags,sizeof tags);
    }
    memcpy(p->current_family,tags,sizeof tags);
    p->queued = BB_PART_NONE; p->ready = 1;
    if(core->running)p->position=(uint8_t)(core->ticks%steps);
    return 1;
}
int bb_parts_init(bb_parts *p,const bb_core *core,unsigned steps) {
    static const uint8_t legacy[BB_MACHINES]={0};
    return bb_parts_init_families(p,core,steps,legacy);
}
int bb_parts_bind_families(bb_parts *p,const uint8_t family[BB_MACHINES]) {
    if(!ready(p) || !families_valid(family))return 0;
    uint8_t tags[BB_MACHINES];memcpy(tags,family,sizeof tags);
    memcpy(p->current_family,tags,sizeof tags);return 1;
}
int bb_parts_store(bb_parts *p, unsigned slot, const bb_core *core, unsigned steps) {
    if (!ready(p) || slot >= BB_PART_COUNT || slot == p->active ||
        slot == p->queued || !phrase_valid(steps) || !bb_session_state_valid(core)) return 0;
    capture(&p->scenes[slot], core); p->phrase_steps[slot] = (uint8_t)steps;
    memcpy(p->family[slot],p->current_family,BB_MACHINES);
    return 1;
}
static void publish(bb_core *core,const bb_core *next) {
    /* Explicit musical fields only; the shared clock and UI never rewind.
     * Revision invalidation follows changed sound bytes, not a stored ID. */
    for (unsigned i = 0; i < BB_MACHINES; i++) {
        uint32_t revision = core->drums[i].sound_revision;
        if (memcmp(&core->drums[i].sound, &next->drums[i].sound, sizeof next->drums[i].sound))
            revision++;
        core->drums[i] = next->drums[i];
        core->drums[i].next = 0; core->drums[i].sound_revision = revision;
    }
    for (unsigned i = 0; i < BB_SYNTHS; i++) {
        core->synths[i] = next->synths[i]; core->synths[i].next = 0;
    }
    core->dillo = next->dillo; core->telegram = next->telegram;
    core->effects = next->effects;
    memcpy(core->band_level, next->band_level, sizeof core->band_level);
    core->bpm = next->bpm; core->swing = next->swing;
    if (!core->swing_odd) core->active_swing = core->swing;
}
static int apply(bb_parts *p, bb_core *core, unsigned slot) {
    const bb_core *next = &p->scenes[slot];
    if (!families_match(p,slot) || !phrase_valid(p->phrase_steps[slot]) || !bb_session_state_valid(next) ||
        !bb_session_state_valid(core)) return BB_PART_REJECTED;
    capture(&p->scenes[p->active], core);
    memcpy(p->family[p->active],p->current_family,BB_MACHINES);
    publish(core,next);
    p->active = (uint8_t)slot; p->queued = BB_PART_NONE; p->position = 0;
    p->launches++;
    return BB_PART_APPLIED;
}
#define distinct bb_wire_separated
int bb_parts_load_trio(bb_parts *p,bb_core *core,const bb_trio_program *source) {
    if(!p || !core || !source || !distinct(p,sizeof *p,core,sizeof *core) ||
       !distinct(p,sizeof *p,source,sizeof *source) || !distinct(core,sizeof *core,source,sizeof *source) ||
       core->sample_rate!=source->seed.sample_rate || !bb_session_state_valid(core) || !bb_trio_valid(source))return 0;
    /* Existing trio data uses 808 lanes. Never reinterpret it through 909. */
    for(unsigned i=0;i<BB_MACHINES;i++)if(p->ready && p->current_family[i])return 0;
    /* No failure-return after the first write. Every constructed state is a
     * validated template with bounded, validated musical fields replaced. */
    for(unsigned slot=0;slot<BB_TRIO_PARTS;slot++) {
        bb_core *dst=&p->scenes[slot];const bb_trio_part *s=&source->part[slot];
        capture(dst,&source->seed);
        bb_machine *drum=&dst->drums[source->primary];
        memcpy(drum->steps,s->drums,sizeof s->drums);drum->length=s->drum_length;
        for(unsigned v=0;v<BB_DRUM_TRACKS;v++)if(s->mode_mask&(1u<<v))
            drum->sound.mode[v]=(uint8_t)((s->mode_value>>v)&1u);
        for(unsigned synth=0;synth<BB_SYNTHS;synth++) {
            dst->synths[synth].length=s->synth_length[synth];
            bb_trio_unpack_notes(dst->synths[synth].notes,s->notes[synth],BB_STEPS);
        }
        p->phrase_steps[slot]=s->phrase;
    }
    memset(p->family,0,sizeof p->family);memset(p->current_family,0,sizeof p->current_family);
    publish(core,&p->scenes[source->initial]);
    p->active=source->initial;p->queued=BB_PART_NONE;p->position=0;p->launches=1;p->ready=1;
    return 1;
}
int bb_parts_request(bb_parts *p, bb_core *core, unsigned slot) {
    if (!ready(p) || !core || slot >= BB_PART_COUNT ||
        !phrase_valid(p->phrase_steps[slot]) || !bb_session_state_valid(&p->scenes[slot]))
        return BB_PART_REJECTED;
    if (slot == p->active) { p->queued = BB_PART_NONE; return BB_PART_UNCHANGED; }
    if(!families_match(p,slot))return BB_PART_REJECTED;
    if (!core->running) return apply(p, core, slot);
    p->queued = (uint8_t)slot;
    return BB_PART_QUEUED;
}
int bb_parts_before_tick(bb_parts *p, bb_core *core) {
    int result = BB_PART_UNCHANGED;
    if (!ready(p) || !core || !core->running) return BB_PART_REJECTED;
    if (!p->position && p->queued != BB_PART_NONE) {
        result = p->queued < BB_PART_COUNT ? apply(p, core, p->queued) : BB_PART_REJECTED;
        if (result == BB_PART_REJECTED) p->queued = BB_PART_NONE;
    }
    p->position = (uint8_t)((p->position + 1u) % p->phrase_steps[p->active]);
    return result;
}
void bb_parts_transport(bb_parts *p) {
    if (!p || p->ready != 1) return;
    p->queued = BB_PART_NONE; p->position = 0;
}
