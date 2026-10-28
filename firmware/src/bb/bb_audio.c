#include "bb_audio.h"
#include "bb_one_drum_profile.h"
#include "bb_audio_family.h"
#include "bb_drum_mix.h"
#ifndef BB_WITH_909_ROUTE
#define BB_WITH_909_ROUTE 0
#endif
#if BB_WITH_909_ROUTE
#include "bb909_audio_route.h"
#endif
#if !BB_USE_DRUM_SPAN || (BB_WITH_909_ROUTE && !defined(BB_GROUP_909_ROUTE_TEST) && !defined(BB_GROUP_TARGET_CANDIDATE))
#error "Group audio bridge only covers the retained 808 span path"
#endif
extern void bb_proposal_group_span(drum808_t *,gg_state *,float *,float *,float *,int);
extern int bb_parallel_begin(const drum808_t *,const gg_state *,unsigned,unsigned);
extern int bb_parallel_finish(unsigned,drum808_t *,gg_state *,float *,float *,float *,unsigned);
extern int bb_parallel_prepare_others(drum808_t *,gg_state *,unsigned,unsigned,unsigned);
extern int bb_parallel_cached_active(unsigned);
extern int bb_parallel_cached_other(unsigned,float *,float *,float *,unsigned);
extern int bb_retained_begin(const drum808_t *,const gg_state *,unsigned,unsigned);
extern int bb_retained_advance(unsigned,unsigned);
extern int bb_retained_finish(unsigned,drum808_t *,gg_state *,float *,float *,float *,unsigned);
extern int bb_retained_prepare_others(drum808_t *,gg_state *,unsigned,unsigned,unsigned,unsigned,unsigned);
#ifndef BB_DILLO_RENDER_QUANTUM
#define BB_DILLO_RENDER_QUANTUM 0
#endif
#if BB_DILLO_RENDER_QUANTUM != 0 && BB_DILLO_RENDER_QUANTUM != 32
#error "Unsupported Dillo physical render quantum"
#endif
#include "bb_profile.h"
#include "bb_audio_trio.h"
#include <string.h>
#if BB_SAMPLE_HOST_INTEGRATION
#if BB_OWNER_AUDIO_BRIDGE || BB_WITH_909_ROUTE
#error "Sample trial must not share the native 808/909 worker path"
#endif
#include "bb_sample_profile_split_r1.h"
bb_sample_audio_binding *bb_audio_sample_layer(const bb_audio *a,unsigned m){
    if(!a)return 0;
    if(!m)return a->sample;
#if defined(BB_SAMPLE_DUAL_TRIAL)
    if(m==1)return a->sample_b;
#endif
    return 0;
}
unsigned bb_audio_sample_layer_id(const bb_audio *a,unsigned m){
    if(!a)return BB_SAMPLE_808_ID;
    if(!m)return a->sample_selected;
#if defined(BB_SAMPLE_DUAL_TRIAL)
    if(m==1&&a->sample_b)return a->sample_b_selected;
#endif
    return BB_SAMPLE_808_ID;
}
static int bb_audio_sample_admit(void *context,const bb_command *c) {
    bb_audio *a=context;
#if BB_WITH_RICH_DRUM_RENDER
    if(!bb_audio_phrase_admit(context,c))return 0;
#endif
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,c->target);
    if(!binding)return 1;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    if(bb_audio_sample_layer_id(a,c->target)==BB_SAMPLE_808_ID)return 1;
#endif
    int reject=0;
    if(c->target<2){
        switch(c->type){
        case BB_CMD_DRUM_STEP:
            reject=(c->value&1u)&&(c->voice>=BB_DRUM_TRACKS||binding->lane_notes[c->voice]==255);break;
        case BB_CMD_DRUM_TOGGLE:
            reject=!c->value&&!(a->core.drums[c->target].steps[c->step].hits&(1u<<c->voice))&&
                binding->lane_notes[c->voice]==255;break;
        case BB_CMD_DRUM_TRIGGER:reject=binding->lane_notes[c->voice]==255;break;
        /* Legacy 808 recipes/pots must not appear to operate the sample bank. */
        case BB_CMD_DRUM_KIT:case BB_CMD_DRUM_PARAMETER:reject=1;break;
#if BB_WITH_RICH_DRUM_RENDER
        case BB_CMD_DRUM_SOURCE_PARAMETER:reject=1;break;
#endif
        default:break;
        }
    }
    if(reject){a->sample_edit_rejected++;a->sample_edit_rejected_type=(uint8_t)c->type;
        a->sample_edit_rejected_lane=c->voice;}
    return !reject;
}
static int bb_audio_sample_config_valid(bb_audio *a,const bb_sample_audio_binding *s) {
    if(!a||!s||!a->groups||
       !s->slots||!s->zones||!s->count||s->count>BB_SK_MAX_SLOTS||!s->nz||s->nz>16||
       s->echo>127||s->spring>127||!(s->gain>=0.0f&&s->gain<=1.0f))return 0;
    if(!bb_sample_separate(a,sizeof *a,s,sizeof *s)||
       !bb_sample_separate(a,sizeof *a,s->slots,s->count*sizeof *s->slots)||
       !bb_sample_separate(a,sizeof *a,s->zones,s->nz*sizeof *s->zones)||
       !bb_sample_separate(s,sizeof *s,s->slots,s->count*sizeof *s->slots)||
       !bb_sample_separate(s->zones,s->nz*sizeof *s->zones,s->slots,s->count*sizeof *s->slots))return 0;
    if(!bb_audio_groups_outside(a,s,sizeof *s)||
       !bb_audio_groups_outside(a,s->slots,s->count*sizeof *s->slots)||
       !bb_audio_groups_outside(a,s->zones,s->nz*sizeof *s->zones))return 0;
    for(unsigned i=0;i<s->nz;i++) {
        bb_sample_voice probe;const bb_sample_zone *z=&s->zones[i];
        if(z->note>127||!bb_sample_start(&probe,z->data,z->bytes,z->frames,z->rate)||
           !bb_sample_separate(a,sizeof *a,z->data,z->bytes)||
           !bb_sample_separate(s->slots,s->count*sizeof *s->slots,z->data,z->bytes)||
           !bb_sample_separate(s,sizeof *s,z->data,z->bytes)||
           !bb_audio_groups_outside(a,z->data,z->bytes))return 0;
        for(unsigned j=0;j<i;j++)if(s->zones[j].note==z->note)return 0;
    }
    for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++) {
        unsigned found=0;
        if(s->lane_notes[lane]==255)continue; /* unsupported, never substituted */
        for(unsigned j=0;j<s->nz;j++)if(s->zones[j].note==s->lane_notes[lane])found++;
        if(found!=1)return 0;
    }
    return 1;
}
int bb_audio_sample_bind(bb_audio *a,bb_sample_audio_binding *s) {
    if(!a||a->sample||a->core.running||a->core.frame||!bb_commands_idle(&a->commands)||
       !bb_audio_sample_config_valid(a,s))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]||a->rich.pending[m].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#endif
    for(unsigned i=0;i<s->count;i++)if(s->slots[i].voice.active)return 0;
    for(unsigned k=0;k<BB_MACHINES;k++)if(drum808_active(&a->kit[k]))return 0;
    a->sample=s;a->commands.admit=bb_audio_sample_admit;a->commands.admit_context=a;return 1;
}
int bb_audio_aux_bind(bb_audio *a,bb_aux_voices *owner) {
    if(!a||!owner||!owner->ready||a->aux||a->core.running||a->core.frame||
       !bb_commands_idle(&a->commands)||
       !bb_audio_groups_outside(a,owner,sizeof *owner))return 0;
    for(unsigned m=0;m<BB_AUX_MACHINES;m++)
        for(unsigned s=0;s<BB_AUX_SLOTS_PER_MACHINE;s++)
            if(owner->slots[m][s].voice.active)return 0;
    a->aux=owner;return 1;
}
uint32_t bb_audio_aux_capabilities(const bb_audio *a) {
    return a&&a->aux&&a->aux->ready?BB_RICH_AUX_VOICES:0;
}
#if defined(BB_SAMPLE_DUAL_TRIAL)
/* Cold-only; immutable catalogue already registered by A. No scene/UI entry. */
static int bb_audio_sample_bind_b_checked(bb_audio *a,bb_sample_audio_binding *s,unsigned id,const uint32_t *rich_voices){
    unsigned descriptor=id==BB_SAMPLE_808_ID?0:id;
    if(!a||!a->sample||a->sample_b||a->core.running||a->core.frame||
       !bb_commands_idle(&a->commands)||descriptor>=a->sample_catalogue_count||
       !bb_audio_sample_config_valid(a,s))return 0;
    bb_sample_audio_binding *first=a->sample;
    if(!bb_sample_separate(first,sizeof *first,s,sizeof *s)||
       !bb_sample_separate(first->slots,first->count*sizeof *first->slots,s,sizeof *s)||
       !bb_sample_separate(first,sizeof *first,s->slots,s->count*sizeof *s->slots)||
       !bb_sample_separate(first->slots,first->count*sizeof *first->slots,s->slots,s->count*sizeof *s->slots))return 0;
    const bb_sample_catalogue_entry *entry=&a->sample_catalogue[descriptor];
    if(s->zones!=entry->zones||s->nz!=entry->nz||memcmp(s->lane_notes,entry->lane_notes,sizeof s->lane_notes))return 0;
    for(unsigned k=0;k<a->sample_catalogue_count;k++){
        const bb_sample_catalogue_entry *bank=&a->sample_catalogue[k];
        if(!bb_sample_separate(s,sizeof *s,bank->zones,bank->nz*sizeof *bank->zones)||
           !bb_sample_separate(s->slots,s->count*sizeof *s->slots,bank->zones,bank->nz*sizeof *bank->zones))return 0;
        for(unsigned z=0;z<bank->nz;z++)
            if(!bb_sample_separate(s,sizeof *s,bank->zones[z].data,bank->zones[z].bytes)||
               !bb_sample_separate(s->slots,s->count*sizeof *s->slots,bank->zones[z].data,bank->zones[z].bytes))return 0;
    }
    for(unsigned i=0;i<s->count;i++)if(s->slots[i].voice.active)return 0;
    for(unsigned i=0;i<BB_MACHINES;i++)if(drum808_active(&a->kit[i]))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]||a->rich.pending[m].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#endif
    if(rich_voices){
        uint32_t available=id==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):bb_audio_sample_source_voices(a,id);
        if(*rich_voices&~available)return 0;
    }else{
        unsigned needed=0;for(unsigned i=0;i<BB_STEPS;i++)needed|=a->core.drums[1].steps[i].hits;
        unsigned supported=id==BB_SAMPLE_808_ID?(1u<<BB_DRUM_TRACKS)-1u:a->sample_catalogue_lanes[id];
        if(needed&~supported)return 0;
    }
    a->sample_b=s;a->sample_b_selected=(uint8_t)id;return 1;
}
int bb_audio_sample_bind_b(bb_audio *a,bb_sample_audio_binding *s,unsigned id){
    return bb_audio_sample_bind_b_checked(a,s,id,NULL);
}
#if BB_WITH_RICH_DRUM_RENDER
static int bb_sample_restore_phrase_voices(const bb_phrase *,uint32_t *);
int bb_audio_sample_bind_b_phrase(bb_audio *a,bb_sample_audio_binding *s,unsigned id,const bb_phrase *phrase){
    uint32_t voices;
    return bb_sample_restore_phrase_voices(phrase,&voices)&&
        bb_audio_sample_bind_b_checked(a,s,id,&voices);
}
#endif
#endif
static int bb_audio_sample_select_consumer(bb_audio *a,const bb_sample_audio_binding *next) {
    /* Prototype audio-consumer entry, not a producer or concurrent UI API.
     * The caller retains both old and new immutable banks through their tails.
     * Only bank descriptors/lane assignment change; no voice or mix reset. */
    if(!a||!a->sample||!next||
       !bb_audio_sample_config_valid(a,next))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    if(a->rich.valid[0]||a->rich.pending[0].ready)return 0;
#endif
    bb_sample_audio_binding *current=a->sample;
    if(next->slots!=current->slots||next->count!=current->count||
       next->gain!=current->gain||next->echo!=current->echo||next->spring!=current->spring||
       !bb_sample_separate(current,sizeof *current,next->zones,next->nz*sizeof *next->zones))return 0;
    for(unsigned i=0;i<next->nz;i++)
        if(!bb_sample_separate(current,sizeof *current,next->zones[i].data,next->zones[i].bytes))return 0;
    /* Validate every stored step, not only the current loop length: extending
     * the loop must not expose an unmapped hit accepted by this selection. */
    unsigned needed=0;
    for(unsigned i=0;i<BB_STEPS;i++)needed|=a->core.drums[0].steps[i].hits;
    if(needed&~((1u<<BB_DRUM_TRACKS)-1u))return 0;
    for(unsigned i=0;i<BB_DRUM_TRACKS;i++)
        if((needed&(1u<<i))&&next->lane_notes[i]==255)return 0;
    *current=*next;return 1;
}
int bb_audio_sample_select_owned(bb_audio *a,const bb_sample_audio_binding *next) {
    int ok=a&&bb_commands_idle(&a->commands)&&bb_audio_sample_select_consumer(a,next);
    if(ok)a->sample_selected=255; /* direct prototype path has no catalogue ID */
    return ok;
}
static int bb_audio_sample_catalogue_valid(bb_audio *a,const bb_sample_audio_binding *current,
                                          const bb_sample_audio_binding *entries,unsigned count) {
    if(!entries||!count||count>8)return 0;
    for(unsigned i=0;i<count;i++){
        const bb_sample_audio_binding *s=&entries[i];
        if(!bb_audio_sample_config_valid(a,s)||s->slots!=current->slots||s->count!=current->count||
           s->gain!=current->gain||s->echo!=current->echo||s->spring!=current->spring||
           !bb_sample_separate(current,sizeof *current,s->zones,s->nz*sizeof *s->zones))return 0;
        for(unsigned j=0;j<s->nz;j++)
            if(!bb_sample_separate(current,sizeof *current,s->zones[j].data,s->zones[j].bytes))return 0;
    }
    return 1;
}
static unsigned bb_sample_source_note(unsigned voice){
    static const uint8_t notes[17]={36,38,45,47,50,64,63,62,37,75,39,70,56,49,46,42,51};
    return voice<17?notes[voice]:255;
}
static void bb_audio_sample_catalogue_publish(bb_audio *a,const bb_sample_audio_binding *entries,unsigned count) {
    for(unsigned i=0;i<count;i++) {
        a->sample_catalogue[i].zones=entries[i].zones;
        a->sample_catalogue[i].nz=(uint8_t)entries[i].nz;
        a->sample_catalogue[i].source_voices=0;
        /* The retained sample banks use these explicit percussion note IDs.
         * A missing identity stays missing; congas never become toms. */
        for(unsigned voice=0;voice<17;voice++)for(unsigned z=0;z<entries[i].nz;z++)
            if(entries[i].zones[z].note==bb_sample_source_note(voice))
                a->sample_catalogue[i].source_voices|=UINT32_C(1)<<voice;
        memcpy(a->sample_catalogue[i].lane_notes,entries[i].lane_notes,sizeof entries[i].lane_notes);
        unsigned lanes=0;
        for(unsigned j=0;j<BB_DRUM_TRACKS;j++)if(entries[i].lane_notes[j]!=255)lanes|=1u<<j;
        a->sample_catalogue_lanes[i]=(uint16_t)lanes;
    }
    a->sample_catalogue_count=(uint8_t)count;a->sample_selected=255;
}
int bb_audio_sample_catalogue_bind(bb_audio *a,const bb_sample_audio_binding *entries,unsigned count) {
    if(!a||!a->sample||a->sample_catalogue_count||a->core.running||a->core.frame||
       !bb_commands_idle(&a->commands)||!bb_audio_sample_catalogue_valid(a,a->sample,entries,count))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    if(a->rich.valid[0]||a->rich.pending[0].ready)return 0;
#endif
    for(unsigned i=0;i<a->sample->count;i++)if(a->sample->slots[i].voice.active)return 0;
    bb_audio_sample_catalogue_publish(a,entries,count);return 1;
}
static uint32_t bb_sample_binding_voices(const bb_sample_audio_binding *entry){
    uint32_t voices=0;
    for(unsigned v=0;v<17;v++)for(unsigned z=0;z<entry->nz;z++)
        if(entry->zones[z].note==bb_sample_source_note(v))voices|=UINT32_C(1)<<v;
    return voices;
}
static int bb_audio_sample_restore_checked(bb_audio *a,const bb_core *core,bb_sample_audio_binding *current,
                                 const bb_sample_audio_binding *entries,unsigned count,unsigned initial,
                                 const uint32_t *rich_voices,unsigned layer){
    int synth=0;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    synth=initial==BB_SAMPLE_808_ID;if(synth)initial=0;
#endif
    if(!a||!core||!current||!entries||layer>=2||initial>=count||!count||count>8||
       core->running||core->frame||core->sample_rate!=44100||
       !bb_commands_idle(&a->commands)||
       !bb_audio_sample_config_valid(a,current)||
       !bb_audio_sample_catalogue_valid(a,&entries[initial],entries,count)||
       entries[initial].slots!=current->slots||entries[initial].count!=current->count||
       !bb_sample_separate(current,sizeof *current,entries,count*sizeof *entries)||
       !bb_sample_separate(core,sizeof *core,current,sizeof *current)||
       !bb_sample_separate(core,sizeof *core,current->slots,current->count*sizeof *current->slots))return 0;
    for(unsigned kit=0;kit<count;kit++){
        const bb_sample_audio_binding *b=&entries[kit];
        if(!bb_sample_separate(current,sizeof *current,b->zones,b->nz*sizeof *b->zones))return 0;
        for(unsigned z=0;z<b->nz;z++)
            if(!bb_sample_separate(current,sizeof *current,b->zones[z].data,b->zones[z].bytes))return 0;
    }
    if(rich_voices){
#if BB_WITH_RICH_DRUM_RENDER
        uint32_t supported=(synth?BB_RICH_808_VOICES:bb_sample_binding_voices(&entries[initial]))|bb_audio_aux_capabilities(a);
        return !(*rich_voices&~supported);
#else
        return 0;
#endif
    }
    unsigned needed=0,supported=0;
    for(unsigned step=0;step<BB_STEPS;step++)needed|=core->drums[layer].steps[step].hits;
    for(unsigned lane=0;lane<BB_DRUM_TRACKS;lane++)if(entries[initial].lane_notes[lane]!=255)supported|=1u<<lane;
    if(synth)supported=(1u<<BB_DRUM_TRACKS)-1u;
    return !(needed&~supported);
}
static int bb_audio_sample_startup_checked(bb_audio *a,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial,
                          const uint32_t *rich_voices) {
    unsigned selected=initial;int synth=0;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    synth=initial==BB_SAMPLE_808_ID;if(synth)initial=0;
#endif
    /* Exclusive cold startup: no IRQ/audio reader may run until this returns.
     * Validate everything before changing audio, admission hooks or binding. */
    if(!a||a->sample||a->sample_catalogue_count||
       (a->commands.admit
#if BB_WITH_RICH_DRUM_RENDER
        &&!(a->commands.admit==bb_audio_phrase_admit&&a->commands.admit_context==a)
#endif
        )||a->core.running||a->core.frame||
       !bb_commands_idle(&a->commands)||!current||initial>=count||count>8||
       !bb_audio_sample_config_valid(a,current)||
       !bb_audio_sample_catalogue_valid(a,current,entries,count)||
       !bb_sample_separate(current,sizeof *current,entries,count*sizeof *entries))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]||a->rich.pending[m].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#endif
    for(unsigned i=0;i<current->count;i++)if(current->slots[i].voice.active)return 0;
    for(unsigned k=0;k<BB_MACHINES;k++)if(drum808_active(&a->kit[k]))return 0;
    unsigned required=0,supported=0;
    for(unsigned i=0;i<BB_STEPS;i++)required|=a->core.drums[0].steps[i].hits;
    for(unsigned i=0;i<BB_DRUM_TRACKS;i++)if(entries[initial].lane_notes[i]!=255)supported|=1u<<i;
    if(synth)supported=(1u<<BB_DRUM_TRACKS)-1u;
    if(rich_voices){
#if BB_WITH_RICH_DRUM_RENDER
        uint32_t available=(synth?BB_RICH_808_VOICES:bb_sample_binding_voices(&entries[initial]))|bb_audio_aux_capabilities(a);
        if(*rich_voices&~available)return 0;
#else
        return 0;
#endif
    }else if(required&~supported)return 0;
    bb_audio_sample_catalogue_publish(a,entries,count);
    *current=entries[initial];a->sample=current;a->sample_selected=(uint8_t)selected;
    a->commands.admit=bb_audio_sample_admit;a->commands.admit_context=a;return 1;
}
int bb_audio_sample_restore_check(bb_audio *a,const bb_core *core,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial){
    return bb_audio_sample_restore_checked(a,core,current,entries,count,initial,NULL,0);
}
int bb_audio_sample_startup(bb_audio *a,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial){
    return bb_audio_sample_startup_checked(a,current,entries,count,initial,NULL);
}
#if BB_WITH_RICH_DRUM_RENDER
static int bb_sample_restore_phrase_voices(const bb_phrase *phrase,uint32_t *voices){
    bb_phrase_index prepared;
    if(!phrase||!bb_phrase_index_prepare(phrase,BB_RICH_REPRESENTABLE_VOICES,&prepared)||
       !bb_audio_phrase_preflight(&prepared))return 0;
    for(unsigned i=phrase->count;i<BB_FACTORY_DRUM_EVENTS;i++)if(phrase->events[i])return 0;
    *voices=prepared.voices;return 1;
}
int bb_audio_sample_restore_check_phrase(bb_audio *a,const bb_core *core,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial,const bb_phrase *phrase){
    uint32_t voices;
    return bb_sample_restore_phrase_voices(phrase,&voices)&&
        bb_audio_sample_restore_checked(a,core,current,entries,count,initial,&voices,0);
}
#if defined(BB_SAMPLE_DUAL_TRIAL)
int bb_audio_sample_restore_check_layer(bb_audio *a,const bb_core *core,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial,
                          unsigned layer,const bb_phrase *phrase){
    uint32_t voices;
    if(phrase){
        if(!bb_sample_restore_phrase_voices(phrase,&voices))return 0;
        return bb_audio_sample_restore_checked(a,core,current,entries,count,initial,&voices,layer);
    }
    return bb_audio_sample_restore_checked(a,core,current,entries,count,initial,NULL,layer);
}
#endif
int bb_audio_sample_startup_phrase(bb_audio *a,bb_sample_audio_binding *current,
                          const bb_sample_audio_binding *entries,unsigned count,unsigned initial,const bb_phrase *phrase){
    uint32_t voices;
    return bb_sample_restore_phrase_voices(phrase,&voices)&&
        bb_audio_sample_startup_checked(a,current,entries,count,initial,&voices);
}
#endif
static int bb_audio_sample_select_registered(bb_audio *a,unsigned layer,unsigned id) {
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,layer);
    if(!binding||layer>=2)return 0;
#if BB_WITH_RICH_DRUM_RENDER
    {
        uint32_t available=id==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):bb_audio_sample_source_voices(a,id);
        if(a->rich.valid[layer]&&(a->rich.active[layer].voices&~available))return 0;
        if(a->rich.pending[layer].ready){
            unsigned slot=a->rich.pending[layer].slot;
            if(slot>=BB_PATTERN_SLOTS||(a->rich.inbox[slot].data.voices&~available))return 0;
        }
    }
#endif
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    if(id==BB_SAMPLE_808_ID)return 1;
#endif
    if(id>=a->sample_catalogue_count)return 0;
    /* Cold registration proves descriptor/range validity and fixed voice
     * ownership. No sample-data access, decoder setup or nested zone scans here.
     * Preserve current mix/sends rather than restoring registration-time values. */
    unsigned needed=0;
#if BB_WITH_RICH_DRUM_RENDER
    if(!a->rich.valid[layer])
#endif
    for(unsigned i=0;i<BB_STEPS;i++)needed|=a->core.drums[layer].steps[i].hits;
    if(needed&~(unsigned)a->sample_catalogue_lanes[id])return 0;
    const bb_sample_catalogue_entry *next=&a->sample_catalogue[id];
    binding->zones=next->zones;binding->nz=next->nz;
    memcpy(binding->lane_notes,next->lane_notes,sizeof next->lane_notes);
    return 1;
}
#if BB_WITH_RICH_DRUM_HANDOFF
int bb_audio_sample_pair_commit_owned(bb_audio *a,unsigned layer,unsigned id,uint32_t voices) {
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,layer);
    if(!binding||layer>=2)return 0;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    if(id==BB_SAMPLE_808_ID){
        if(voices&~(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)))return 0;
    }else
#endif
    {
        if(id>=a->sample_catalogue_count ||
           (voices&~bb_audio_sample_source_voices(a,id)))return 0;
        const bb_sample_catalogue_entry *next=&a->sample_catalogue[id];
        if(!next->zones||!next->nz)return 0;
        binding->zones=next->zones;binding->nz=next->nz;
        memcpy(binding->lane_notes,next->lane_notes,sizeof next->lane_notes);
    }
#if defined(BB_SAMPLE_DUAL_TRIAL)
    if(layer==1){
        a->sample_b_requested=(uint8_t)id;a->sample_b_selected=(uint8_t)id;
        a->sample_b_select_result=1;a->sample_b_select_serial++;return 1;
    }
#endif
    a->sample_requested=(uint8_t)id;a->sample_selected=(uint8_t)id;
    a->sample_select_result=1;a->sample_select_serial++;return 1;
}
#endif
#if BB_WITH_RICH_DRUM_RENDER
#include "bb_voice_fallback.h"
uint32_t bb_audio_sample_source_voices(const bb_audio *a,unsigned id){
    uint32_t native=a&&id<a->sample_catalogue_count?
        a->sample_catalogue[id].source_voices|bb_audio_aux_capabilities(a):0;
    /* Every percussion source resolves to a real bound voice. Empty or
     * invalid banks remain invalid; structural validation is unchanged. */
    return (native & BB_RICH_REPRESENTABLE_VOICES) ? BB_RICH_REPRESENTABLE_VOICES : 0;
}
int bb_audio_sample_phrase_hit(bb_audio *a,unsigned voice,float velocity){
    return bb_audio_sample_phrase_hit_layer(a,0,voice,velocity);
}
int bb_audio_sample_phrase_hit_layer(bb_audio *a,unsigned layer,unsigned voice,float velocity){
    bb_sample_audio_binding *s=bb_audio_sample_layer(a,layer);
    if(!s||voice>=BB_PHRASE_VOICES||!(velocity>0.f&&velocity<=1.f))return 0;
    unsigned id=bb_audio_sample_layer_id(a,layer);
    if(id!=BB_SAMPLE_808_ID&&id>=a->sample_catalogue_count)return 0;
    voice=bb_voice_fallback(voice,(id==BB_SAMPLE_808_ID?BB_RICH_808_NATIVE_VOICES:a->sample_catalogue[id].source_voices)|
                          bb_audio_aux_capabilities(a));
    if(voice==BB_FALLBACK_NONE)return 0;
    unsigned level=(unsigned)(velocity*127.f+0.5f);if(!level)level=1;
    if(voice==BB_AUX_SY||voice==BB_AUX_ZAP){
        if(!bb_audio_aux_capabilities(a))return 0;
        int result=bb_aux_voices_trigger(a->aux,layer,voice,level);
        if(result==BB_SK_FULL){a->sample_rejected++;return 1;} /* bounded omitted hit, audio continues */
        if(result!=BB_SK_OK)a->sample_rejected++;
        return result==BB_SK_OK;
    }
    if(voice>=17)return 0;
    int result=bb_sample_kit_trigger(s->slots,s->count,s->zones,s->nz,bb_sample_source_note(voice),level);
    /* A full pool is bounded polyphony exhaustion, not corrupt renderer state.
     * Keep existing tails; count the omitted hit exactly as the auxiliary bank
     * does. Invalid data and missing mappings still refuse the strike. */
    if(result!=BB_SK_OK)a->sample_rejected++;
    return result==BB_SK_OK || result==BB_SK_FULL;
}
#endif
int bb_audio_sample_status(bb_audio *a,bb_sample_status *out) {
    return bb_audio_sample_status_layer(a,0,out);
}
int bb_audio_sample_status_layer(bb_audio *a,unsigned layer,bb_sample_status *out) {
    if(!a||!out||layer>=2||(!a->commands.enter!=!a->commands.leave)||
       !bb_sample_separate(a,sizeof *a,out,sizeof *out)||
       !bb_audio_groups_outside(a,out,sizeof *out))return 0;
    /* Caller-owned destination must not overwrite bound control/audio storage. */
    for(unsigned owner=0;owner<2;owner++)if(bb_audio_sample_layer(a,owner)){
        bb_sample_audio_binding *s=bb_audio_sample_layer(a,owner);
        if(!bb_sample_separate(s,sizeof *s,out,sizeof *out)||
           !bb_sample_separate(s->slots,s->count*sizeof *s->slots,out,sizeof *out)||
           !bb_sample_separate(s->zones,s->nz*sizeof *s->zones,out,sizeof *out))return 0;
        for(unsigned z=0;z<s->nz;z++)
            if(!bb_sample_separate(s->zones[z].data,s->zones[z].bytes,out,sizeof *out))return 0;
        for(unsigned k=0;k<a->sample_catalogue_count;k++){
            const bb_sample_catalogue_entry *c=&a->sample_catalogue[k];
            if(!bb_sample_separate(c->zones,c->nz*sizeof *c->zones,out,sizeof *out))return 0;
            for(unsigned z=0;z<c->nz;z++)
                if(!bb_sample_separate(c->zones[z].data,c->zones[z].bytes,out,sizeof *out))return 0;
        }
    }
    bb_sample_status v={0};uint16_t masks[8]={0};uint16_t source_compatible=255;
    uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,layer);
    int ready=binding&&a->sample_catalogue_count;
    if(ready){
        v.serial=a->sample_select_serial;v.edit_rejections=a->sample_edit_rejected;
        v.count=a->sample_catalogue_count;v.selected=a->sample_selected;
        v.requested=a->sample_requested;v.result=a->sample_select_result;
        v.machine=(uint8_t)layer;
#if defined(BB_SAMPLE_DUAL_TRIAL)
        if(layer==1){
            v.serial=a->sample_b_select_serial;v.selected=a->sample_b_selected;
            v.requested=a->sample_b_requested;v.result=a->sample_b_select_result;
        }
#endif
        v.echo=(uint8_t)binding->echo;v.spring=(uint8_t)binding->spring;v.gain=binding->gain;
        v.native_compatible=1;
        v.level=a->core.drums[layer].level;
        v.queue_idle=(uint8_t)bb_commands_idle(&a->commands);
        v.stop_pending=(uint8_t)(a->commands.stop_pending||a->commands.stop_inflight);
        /* Rich phrases supply their own voice mask below. Do not scan the
         * inactive legacy grid under the IRQ guard only to discard it. */
#if BB_WITH_RICH_DRUM_RENDER
        if(!a->rich.valid[layer])
#endif
        for(unsigned i=0;i<BB_STEPS;i++)v.required_lanes|=a->core.drums[layer].steps[i].hits;
        for(unsigned i=0;i<BB_DRUM_TRACKS;i++)if(binding->lane_notes[i]!=255)v.supported_lanes|=1u<<i;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        if(v.selected==BB_SAMPLE_808_ID)v.supported_lanes=(1u<<BB_DRUM_TRACKS)-1u;
#endif
        memcpy(masks,a->sample_catalogue_lanes,sizeof masks);
#if BB_WITH_RICH_DRUM_RENDER
        uint32_t voices=a->rich.valid[layer]?a->rich.active[layer].voices:0;
        if(a->rich.valid[layer])v.required_lanes=0;
        if(a->rich.pending[layer].ready){
            unsigned slot=a->rich.pending[layer].slot;
            if(slot<BB_PATTERN_SLOTS)voices|=a->rich.inbox[slot].data.voices;
            else {source_compatible=0;v.native_compatible=0;}
        }
        v.native_compatible&=(uint8_t)!(voices&~(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)));
        for(unsigned i=0;i<v.count;i++)if(voices&~bb_audio_sample_source_voices(a,i))
            source_compatible&=(uint16_t)~(1u<<i);
#endif
    }
    if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
    if(!ready)return 0;
    v.native_compatible&=(uint8_t)!(v.required_lanes&~((1u<<BB_DRUM_TRACKS)-1u));
    for(unsigned i=0;i<v.count;i++)if((source_compatible&(1u<<i))&&!(v.required_lanes&~masks[i]))v.compatible|=1u<<i;
    *out=v;return 1;
}
int bb_audio_sample_mix_command(const bb_sample_status *view,unsigned parameter,
                                 unsigned value,bb_command *out){
    if(!view||!out||!view->count||view->count>8||view->selected>=view->count||
       view->machine>=2||
       !view->queue_idle||view->stop_pending||parameter>=BB_SAMPLE_MIX_COUNT||
       value>(parameter==BB_SAMPLE_MIX_GAIN?100u:127u)||
       (parameter==BB_SAMPLE_MIX_GAIN&&!value)||
       !bb_sample_separate(view,sizeof *view,out,sizeof *out))return 0;
    bb_command c={0};c.type=BB_CMD_SAMPLE_MIX;c.step=view->selected;c.target=view->machine;
    c.voice=(uint8_t)parameter;c.value=(uint16_t)value;
    c.note.note=(uint8_t)view->serial;c.note.gate=(uint8_t)(view->serial>>8);
    c.note.accent=(uint8_t)(view->serial>>16);c.note.slide=(uint8_t)(view->serial>>24);
    *out=c;return 1;
}
#endif
#ifndef BB_OWNER_AUDIO_BRIDGE
#define BB_OWNER_AUDIO_BRIDGE 0
#endif
#if BB_OWNER_AUDIO_BRIDGE
#if BB_WITH_909_ROUTE
#error "Persistent-owner bridge requires explicit native909 ownership integration"
#endif
#include "audio-bridge.h"
#endif
#if BB_WITH_PARTS
static void audio_part_command(bb_audio *,const bb_command *);
static void audio_part_changed(bb_audio *);
#endif
/* Map portable parameters onto the pinned engine's descriptor ordinals.
 * No resets or triggers here. Apply only changed pots at block boundaries;
 * temporarily selecting alternate sound banks never modifies lane.snd or
 * its ringing DSP state. Both banks are restored before scheduling hits. */
static void audio_drum_sound(drum808_t *d,const bb_drum_sound *s) {
    static const int8_t map[BB_DRUM_TRACKS][BB_DRUM_PARAMETER_COUNT]={
        {0,3,2,5,6,1,4,-1,-1,7,8},
        {0,3,4,5,6,1,-1,2,-1,7,8},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,4,5,-1,-1,-1,3,6,7},
        {0,1,2,5,6,-1,3,-1,4,7,8},
        {0,1,2,3,4,-1,-1,-1,-1,5,6},
        {0,2,1,3,4,-1,-1,-1,-1,5,6},
        {0,2,1,3,4,-1,-1,-1,-1,5,6},
        {0,1,2,3,4,-1,-1,-1,-1,5,6}};
    unsigned track,mode,p;
    for(track=0;track<BB_DRUM_TRACKS;track++) {
        unsigned banks=map[track][BB_DRUM_MODE]>=0?2u:1u;
        for(mode=0;mode<banks;mode++) {
            unsigned sound=bb_drum_sound_variant(track,mode);
            /* A mode switch only selects a bank. Leave unchanged banks alone;
             * the final switch restoration below still applies the requested
             * mode. CP's attack descriptor addresses MA even in clap mode. */
            if(!memcmp(d->pot[sound],s->pot[sound],BB_DRUM_POTS) &&
               (track!=6 || d->pot[D8S_MA][D8P_X1]==s->pot[D8S_MA][D8P_X1]))continue;
            if(banks==2 && d->sw[track]!=mode)drum808_set(d,(int)track,map[track][BB_DRUM_MODE],(int)mode);
            for(p=0;p<BB_DRUM_PARAMETER_COUNT;p++) {
                int ordinal=map[track][p];unsigned value;
                if(ordinal<0 || p==BB_DRUM_MODE)continue;
                /* Canonical pot order is shared with the pinned DSP. The
                 * helper getter resolves snare/BD tone and fixed MA attack. */
                switch(p) {
                case BB_DRUM_LEVEL:value=s->pot[sound][D8P_LEVEL];break;
                case BB_DRUM_TUNE:value=s->pot[sound][D8P_TUNE];break;
                case BB_DRUM_DECAY:value=s->pot[sound][D8P_DECAY];break;
                case BB_DRUM_DRIVE:value=s->pot[sound][D8P_DRIVE];break;
                case BB_DRUM_DIST:value=s->pot[sound][D8P_DIST];break;
                case BB_DRUM_TONE:value=s->pot[sound][track==0?D8P_X1:D8P_X2];break;
                case BB_DRUM_ATTACK:value=track==6?s->pot[D8S_MA][D8P_X1]:s->pot[sound][D8P_X2];break;
                case BB_DRUM_SNAPPY:value=s->pot[sound][D8P_X1];break;
                case BB_DRUM_SPRING:value=s->pot[sound][D8P_REV];break;
                default:value=s->pot[sound][D8P_DLY];break;
                }
                if(drum808_get(d,(int)track,ordinal)!=(int)value)drum808_set(d,(int)track,ordinal,(int)value);
            }
        }
        if(banks==2 && d->sw[track]!=s->mode[track])drum808_set(d,(int)track,map[track][BB_DRUM_MODE],s->mode[track]);
    }
    if(d->kpot[0]!=s->kit_level)drum808_set(d,D8_KIT,0,s->kit_level);
    if(d->kpot[1]!=s->kit_accent)drum808_set(d,D8_KIT,1,s->kit_accent);
}
static void audio_hit(void *ctx,const bb_hit *e) {
    bb_audio *a=ctx;
#if BB_SAMPLE_HOST_INTEGRATION
    if(bb_audio_sample_layer(a,e->machine)
#if defined(BB_SAMPLE_HYBRID_TRIAL)
       &&bb_audio_sample_layer_id(a,e->machine)!=BB_SAMPLE_808_ID
#endif
       ) {
        bb_sample_audio_binding *s=bb_audio_sample_layer(a,e->machine);
        int result=e->voice<BB_DRUM_TRACKS?bb_sample_kit_trigger(s->slots,s->count,s->zones,s->nz,
            s->lane_notes[e->voice],e->velocity):BB_SK_INVALID;
        if(result==BB_SK_OK)a->hits[e->machine]++;else a->sample_rejected++;
        return;
    }
#endif
#if BB_WITH_909_ROUTE
    if((int)e->machine==bb909_audio_layer()) {
        if(bb909_audio_hit(e->machine,e->voice,e->velocity))a->hits[e->machine]++;
        return;
    }
#endif
    if(e->machine>=BB_MACHINES || e->voice>=D8_NUM)return;
#if BB_OWNER_AUDIO_BRIDGE
    int handled=bb_owner_audio_hit(a,e);
    if(handled){if(handled>0)a->hits[e->machine]++;return;}
#endif
    drum808_trigger(&a->kit[e->machine],e->voice,
        e->velocity==127?1.0f:e->velocity*(D8_VEL_NORMAL/88.0f));
    a->hits[e->machine]++;
}
#if BB_WITH_RICH_DRUM_RENDER
static void audio_sequence_hit(void *ctx,const bb_hit *e){
    bb_audio *a=ctx;
    if(e->machine<BB_RICH_DRUMS&&a->rich.valid[e->machine])return;
    audio_hit(ctx,e);
}
#endif
static void audio_sync_drum(bb_audio *a,unsigned k) {
#if BB_SAMPLE_HOST_INTEGRATION
    if(bb_audio_sample_layer(a,k)
#if defined(BB_SAMPLE_HYBRID_TRIAL)
       &&bb_audio_sample_layer_id(a,k)!=BB_SAMPLE_808_ID
#endif
       )return; /* never send 808 pots into sample data */
#endif
#if BB_OWNER_AUDIO_BRIDGE
    if(bb_owner_audio_sync(a,k))return;
#endif
#if BB_WITH_909_ROUTE
    if((int)k==bb909_audio_layer())return; /* never interpret 808 pots as 909 */
#endif
    if(a->drum_revision[k]!=a->core.drums[k].sound_revision) {
        audio_drum_sound(&a->kit[k],&a->core.drums[k].sound);
        a->drum_revision[k]=a->core.drums[k].sound_revision;
    }
}
#if BB_WITH_DILLO || BB_WITH_TELEGRAM
static void audio_release_synth(bb_audio *a,unsigned synth) {
    if(a->keys[synth].count)
        memset(a->keys[synth].owners,0,sizeof a->keys[synth].owners);
    a->keys[synth].count=0;
#if BB_WITH_DILLO
    if(synth==0)bb_dillo_stop(&a->dillo);
#endif
#if BB_WITH_TELEGRAM
    if(synth==1)bb_telegram_stop(&a->telegram);
#endif
}
static void audio_key_voice(bb_audio *a,unsigned s,int legato) {
    unsigned n=a->keys[s].count;
    if(!n){audio_release_synth(a,s);return;}
    unsigned note=a->keys[s].order[n-1u],velocity=a->keys[s].velocity[note];
#if BB_WITH_DILLO
    if(s==0) {
        if(memcmp(&a->dillo.sound,&a->core.dillo,sizeof a->core.dillo))
            bb_dillo_sound(&a->dillo,&a->core.dillo);
        bb_dillo_key(&a->dillo,note,velocity,legato);
    }
#endif
#if BB_WITH_TELEGRAM
    if(s==1) {
        if(memcmp(&a->telegram.sound,&a->core.telegram,sizeof a->core.telegram))
            bb_telegram_sound(&a->telegram,&a->core.telegram);
        /* Finite unswung sixteenth-note envelope basis, even while stopped. */
        bb_telegram_key(&a->telegram,note,velocity,legato,
            a->core.sample_rate*60u/((uint32_t)a->core.bpm*4u));
    }
#endif
}
static void audio_key(bb_audio *a,const bb_command *c) {
    unsigned s=c->target,n=a->keys[s].count,i=0,was_top;
    uint8_t bit=(uint8_t)(1u<<c->step);
    if(a->core.synths[s].muted)return;
    if(!c->value) {
        /* An unrelated/duplicate off cannot remove another source's pitch,
         * change priority or retrigger an envelope. Shared releases are silent. */
        if(!(a->keys[s].owners[c->voice]&bit))return;
        a->keys[s].owners[c->voice]&=(uint8_t)~bit;
        if(a->keys[s].owners[c->voice])return;
    } else a->keys[s].owners[c->voice]|=bit;
    while(i<n && a->keys[s].order[i]!=c->voice)i++;
    was_top=i<n && i+1u==n;
    if(i<n) {
        /* Forward overlap copy, at most127 bytes, no target memmove. */
        for(unsigned j=i;j+1u<n;j++)a->keys[s].order[j]=a->keys[s].order[j+1u];
        a->keys[s].count=(uint16_t)--n;
    }
    if(c->value) {
        a->keys[s].order[n++]=c->voice;
        a->keys[s].velocity[c->voice]=(uint8_t)c->value;
        a->keys[s].count=(uint16_t)n;
    } else if(!was_top)return;
    audio_key_voice(a,s,c->value?(n>1u || was_top):1);
}
static void audio_release_source(bb_audio *a,unsigned s,unsigned source) {
    unsigned n=a->keys[s].count,kept=0;
    uint8_t mask=(uint8_t)~(1u<<source);
    if(!n)return;
    unsigned top=a->keys[s].order[n-1u];
    /* One stable compaction, not up to128 independent note-off searches.
     * A surviving top note is not rearticulated when another source departs. */
    for(unsigned i=0;i<n;i++) {
        unsigned note=a->keys[s].order[i];
        a->keys[s].owners[note]&=mask;
        if(a->keys[s].owners[note])a->keys[s].order[kept++]=(uint8_t)note;
    }
    a->keys[s].count=(uint16_t)kept;
    if(!a->keys[s].owners[top])audio_key_voice(a,s,1);
}
#endif
static void audio_live_command(void *ctx,const bb_command *c) {
    bb_audio *a=ctx;
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
    if(c->type==BB_CMD_DRUM_PHRASE
#if BB_SAMPLE_HOST_INTEGRATION
       ||c->type==BB_CMD_KIT_PHRASE
#endif
       ){
        bb_audio_phrase_command(a,c);return;
    }
    if(c->type==BB_CMD_TRANSPORT)bb_audio_phrase_cancel(a);
#if BB_WITH_RICH_DRUM_RENDER
    if(c->type==BB_CMD_TRANSPORT)bb_audio_phrase_transport(a,c->value);
#endif
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    if(c->type==BB_CMD_SAMPLE_MIX){
        bb_sample_audio_binding *binding=bb_audio_sample_layer(a,c->target);
        uint32_t completed=a->sample_select_serial;
#if defined(BB_SAMPLE_DUAL_TRIAL)
        if(c->target==1)completed=a->sample_b_select_serial;
#endif
        uint32_t serial=(uint32_t)c->note.note|((uint32_t)c->note.gate<<8)|
            ((uint32_t)c->note.accent<<16)|((uint32_t)c->note.slide<<24);
        if(!binding || !a->sample_catalogue_count || c->step>=a->sample_catalogue_count ||
           c->step!=bb_audio_sample_layer_id(a,c->target) || serial!=completed){
            a->sample_edit_rejected++;a->sample_edit_rejected_type=(uint8_t)c->type;
            a->sample_edit_rejected_lane=255;return;
        }
        if(c->voice==BB_SAMPLE_MIX_ECHO)binding->echo=c->value;
        else if(c->voice==BB_SAMPLE_MIX_SPRING)binding->spring=c->value;
        else if(c->voice==BB_SAMPLE_MIX_GAIN)binding->gain=(float)c->value*(1.0f/100.0f);
        return;
    }
    if(c->type==BB_CMD_SAMPLE_KIT){
        int ok=bb_audio_sample_select_registered(a,c->target,c->value);
#if defined(BB_SAMPLE_DUAL_TRIAL)
        if(c->target==1){
            a->sample_b_requested=(uint8_t)c->value;
            a->sample_b_select_result=(uint8_t)(ok?1:2);
            if(ok)a->sample_b_selected=(uint8_t)c->value;
            a->sample_b_select_serial++;return;
        }
#endif
        a->sample_requested=(uint8_t)c->value;
        a->sample_select_result=(uint8_t)(ok?1:2);
        if(ok)a->sample_selected=(uint8_t)c->value;
        a->sample_select_serial++;return;
    }
#endif
    if(c->type==BB_CMD_GROUP_LEVEL || c->type==BB_CMD_GROUP_MUTE) {
#if BB_OWNER_AUDIO_BRIDGE
        if(bb_owner_audio_group(a,c))return;
#endif
        /* Validation belongs to the existing bounded command FIFO. */
        gg_fader *g=&a->groups[c->target].fader[c->voice];
        (void)gg_set(&a->groups[c->target],c->voice,
            c->type==BB_CMD_GROUP_LEVEL?c->value:g->level,
            c->type==BB_CMD_GROUP_MUTE?c->value:g->muted);
        return;
    }
    if(c->type==BB_CMD_SYNTH_TRANSPOSE) {
        a->transpose[c->target]=(int8_t)((int)c->value-BB_TRANSPOSE_LIMIT);return;
    }
#if BB_WITH_PATTERN
    if(c->type==BB_CMD_NOTE) a->patterns.expression[c->target][c->step]=0;
    if(c->type==BB_CMD_SYNTH_PATTERN){bb_audio_pattern_command(a,c);return;}
    if(c->type==BB_CMD_DRUM_PATTERN){bb_audio_drum_pattern_command(a,c);return;}
    if(c->type==BB_CMD_TRANSPORT)bb_audio_pattern_transport(a);
#endif
#if BB_WITH_PARTS
    if(c->type==BB_CMD_TRIO_BANK) {
        int result=bb_audio_trio_command(a,c);
        if(result==BB_TRIO_APPLIED){a->parts_result=BB_PART_APPLIED;audio_part_changed(a);}
        return;
    }
    if(c->type==BB_CMD_PART_LAUNCH){audio_part_command(a,c);return;}
    if(c->type==BB_CMD_TRANSPORT) {
#if BB_WITH_PREPARED_BANK
        if(a->parts.parts.ready && a->parts.parts.queued!=BB_PART_NONE)a->parts_result=BB_PART_CANCELLED;
        /* Release the pending trio while its prepared ownership is visible. */
        bb_audio_trio_transport(a);pb_cancel(&a->parts);
#else
        if(a->parts.ready && a->parts.queued!=BB_PART_NONE)a->parts_result=BB_PART_CANCELLED;
        bb_parts_transport(&a->parts);bb_audio_trio_transport(a);
#endif
    }
#endif
#if BB_WITH_DILLO || BB_WITH_TELEGRAM
    if(c->type==BB_CMD_SYNTH_KEY){audio_key(a,c);return;}
    if(c->type==BB_CMD_SYNTH_SOURCE_RELEASE) {
        audio_release_source(a,c->target,c->step);return;
    }
    if(c->type==BB_CMD_SYNTH_RELEASE) {
        audio_release_synth(a,c->target);return;
    }
    if(c->type==BB_CMD_SYNTH_MUTE && c->value) {
        if(a->keys[c->target].count)audio_release_synth(a,c->target);
        return;
    }
    if(c->type==BB_CMD_TRANSPORT && !c->value)
        for(unsigned s=0;s<BB_SYNTHS;s++)
            if(a->keys[s].count)audio_release_synth(a,s);
#endif
#if BB_WITH_EVOLVE
    if(c->type==BB_CMD_EVOLVE){bb_audio_evolve_command(a,c);return;}
    if(c->type==BB_CMD_TRANSPORT) {
        /* A partial pre-stop phrase must not count as a completed phrase
         * after restarting at step zero. Preserve home, RNG and controls. */
        memset(a->evolve.seen,0,sizeof a->evolve.seen);
        memset(a->evolve.phrases,0,sizeof a->evolve.phrases);
        return;
    }
#endif
#if BB_WITH_909_ROUTE
    if(c->type==BB_CMD_909_PARAMETER) {
        uint32_t epoch=(uint32_t)c->note.note|((uint32_t)c->note.gate<<8)
            |((uint32_t)c->note.accent<<16)|((uint32_t)c->note.slide<<24);
        (void)bb909_audio_parameter_epoch(c->target,c->voice,c->step,c->value,epoch);
        return;
    }
#endif
    if(c->type!=BB_CMD_DRUM_TRIGGER)return;
    bb_hit e={0};
    /* Earlier sound/mode commands must reach the engine before it latches a
     * hit. Later edits may change a ringing voice just like ordinary pot edits.
     * No UI DSP calls, second queue, pattern writes or transport changes. */
    audio_sync_drum(a,c->target);
    e.frame=a->core.frame;e.machine=c->target;e.voice=c->voice;e.velocity=(uint8_t)c->value;
    audio_hit(a,&e);
}
static const float *audio_band_fader(bb_audio *a,unsigned channel,unsigned frames,float *fixed) {
    uint32_t current=a->band_gain[channel];
    uint32_t target=(a->core.band_muted&(1u<<channel))?0u:((uint32_t)a->core.band_level[channel]*65536u+63u)/127u;
    *fixed=(float)current*(1.0f/65536.0f);
    if(current==target)return 0;
    for(unsigned i=0;i<frames;i++) {
        if(current<target)current+=target-current>256u?256u:target-current;
        if(current>target)current-=current-target>256u?256u:current-target;
        a->band_curve[i]=(float)current*(1.0f/65536.0f);
    }
    a->band_gain[channel]=current;
    return a->band_curve;
}
static int one_drum_audio_ok(const bb_audio *a) {
    if(!bb_one_drum_core_ok(&a->core))return 0;
    for(unsigned k=
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        2
#else
        1
#endif
        ;k<BB_MACHINES;k++)if(drum808_active(&a->kit[k]))return 0;
    return 1;
}
#if BB_WITH_RICH_DRUM_RENDER
/* Render one event-bounded interval. Both backends retain tails across sound
 * changes; only the sample bus advances the shared faders when it is bound. */
int bb_audio_phrase_buses(bb_audio *a,unsigned machine,gg_state *groups,
                        float *dry,float *rev,float *dly,unsigned frames) {
    if(machine<2)BB_STAGE_BEGIN(BB_STAGE_DRUM_A+machine);
#if BB_SAMPLE_HOST_INTEGRATION
    if(bb_audio_sample_layer(a,machine)){
        gg_state pre_span=*groups;
        if(drum808_active(&a->kit[machine])){
            gg_state synth_groups=*groups;
            bb_proposal_group_span(&a->kit[machine],&synth_groups,dry,rev,dly,(int)frames);
        }
        bb_sample_audio_binding *s=bb_audio_sample_layer(a,machine);
        if(bb_sample_profile_buses(s->slots,s->count,groups,s->gain,s->echo,s->spring,
                dry,dly,rev,frames,1)!=BB_SK_OK){
            if(machine<2)BB_STAGE_END(BB_STAGE_DRUM_A+machine,frames);
            a->sample_rejected++;return 0;
        }
        if(bb_aux_voices_active(a->aux,machine)&&
           bb_aux_voices_render(a->aux,machine,&pre_span,s->gain,s->echo,s->spring,
                                dry,dly,rev,frames)!=BB_SK_OK){
            if(machine<2)BB_STAGE_END(BB_STAGE_DRUM_A+machine,frames);
            a->sample_rejected++;return 0;
        }
        if(machine<2)BB_STAGE_END(BB_STAGE_DRUM_A+machine,frames);
        return 1;
    }
#endif
    bb_proposal_group_span(&a->kit[machine],groups,dry,rev,dly,(int)frames);
    if(machine<2)BB_STAGE_END(BB_STAGE_DRUM_A+machine,frames);
    return 1;
}
#endif
uint32_t bb_audio_boundary_flags; /* see bb_audio.h; audio owner only */
#if BB_WITH_PATTERN
/* One copy of the boundary rule used by bb_audio_drum_pattern_tick and
 * bb_audio_phrase_tick (both test the position the NEXT tick will use,
 * a->patterns.position). Shared by the P1b quiet predicate and the
 * boundary-class instrument so the two can never disagree (PR #19 F4). */
static int bb_audio_lands(unsigned phrase,unsigned position) {
    return phrase==48u?position%48u==0u:!(position&(phrase-1u));
}
static int bb_audio_b_launch_due(const bb_audio *a,unsigned position) {
    if(a->drum_patterns.pending[1].ready && bb_audio_lands(a->drum_patterns.pending[1].phrase,position))return 1;
#if BB_WITH_RICH_DRUM_HANDOFF
    if(a->rich.pending[1].ready && bb_audio_lands(a->rich.pending[1].phrase,position))return 1;
#endif
    return 0;
}
#endif
#if BB_WITH_RICH_DRUM_RENDER && (defined(BB_GROUP_TARGET_CANDIDATE) || defined(BB_RICH_WORKER_HOST))
#include "rich-schedule-plan-prototype.h"
#define BB_RICH_WORKER_ENABLED 1
_Static_assert(sizeof(bb_rsp_state)==sizeof(((bb_audio*)0)->rich.player[0]),"worker player size");
#define BB_RICH_OFFSET_CHECK(f) _Static_assert(offsetof(bb_rsp_state,f)==offsetof(__typeof__(((bb_audio*)0)->rich.player[0]),f),"worker player offset")
BB_RICH_OFFSET_CHECK(queue);BB_RICH_OFFSET_CHECK(phase);BB_RICH_OFFSET_CHECK(generation);
BB_RICH_OFFSET_CHECK(primary);BB_RICH_OFFSET_CHECK(delayed);BB_RICH_OFFSET_CHECK(step);
BB_RICH_OFFSET_CHECK(pending);BB_RICH_OFFSET_CHECK(odd);BB_RICH_OFFSET_CHECK(swing);BB_RICH_OFFSET_CHECK(sounded);
extern int bb_rich_worker_begin(const bb_phrase_index *,const void *,const drum808_t *,const gg_state *,unsigned,unsigned,uint64_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern int bb_rich_worker_finish(unsigned,unsigned,unsigned,void *,drum808_t *,gg_state *,float *,float *,float *,uint32_t *);
extern void bb_rich_worker_cleanup(void);
extern int bb_rich_worker_recovery_pending(void);
/* Timing P1b: one drum-B worker transaction per 128-frame call.
 * When a sequencer step boundary splits a call, B is published ONCE for the
 * whole remaining call instead of once per span, provided the boundary's
 * audio_before_tick cannot change any B input (no B pattern or phrase is
 * pending; parts/trio are compiled out). The worker renders straight through
 * the boundary; the rich scheduler resolves the step's hits at their sample
 * offsets (engineering/rich-block-equivalence: one job == any two-span split).
 * B's mix is deferred to the job's last span and replayed per span, in span
 * order, with each span's own fader curve and gain target, so every output
 * sample receives Dillo, kit A and kit B additions in the original order.
 * After every internal boundary B's live inputs are compared with CPU0's copy
 * taken at publication; any difference latches the existing worker failure
 * (block rejected, never replayed), exactly like a failed join. */
#ifndef BB_P1B_CALL_JOB
#if !BB_WITH_PARTS && !BB_OWNER_AUDIO_BRIDGE && !BB_WITH_909_ROUTE && BB_USE_DRUM_SPAN && BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
#define BB_P1B_CALL_JOB 1
#else
#define BB_P1B_CALL_JOB 0
#endif
#endif
#if BB_P1B_CALL_JOB
#define BB_P1B_SEGMENTS 4u /* 30..300 BPM at 44.1 kHz: a step is >= 1,102 frames, so <= 2 spans per call */
#define BB_P1B_INPUTS 14u
typedef struct {uint32_t offset,frames,target,curved;float fixed;} bb_p1b_segment;
/* CPU0-owned audio state. CPU1 never reads or writes it; no cache maintenance. */
static struct {
    uint32_t call_frames;  /* frames of the bb_audio_render call in progress */
    uint32_t active;       /* a call-level B job is published and not yet joined */
    uint32_t carry;        /* the span just completed normally with that job still in flight */
    uint32_t first,end;    /* call offsets [first,end) rendered by the job */
    uint64_t first_frame;  /* core frame at publication: recovery reconciles from here */
    uint32_t recover;      /* a call-level job failed: reconcile B from first_frame */
    uint32_t rejected;     /* a guard trip rejected this call: later spans render nothing */
    uint32_t count;        /* recorded B mix segments (one per span) */
    bb_p1b_segment seg[BB_P1B_SEGMENTS];
    float curve[128];      /* drum-bus fader samples of moving segments, by call offset */
    uint32_t inputs[BB_P1B_INPUTS];
    bb_rsp_state player;   /* CPU0 copy of live B player at publication */
    gg_state groups;       /* CPU0 copy of live B groups at publication */
#ifdef BB_P1B_HOST_FULL_GUARD
    drum808_t kit;bb_phrase_index phrase; /* host proof that the cheap guard suffices */
#endif
} bb_p1b;
#ifdef BB_P1B_HOST_FULL_GUARD
uint32_t bb_p1b_host_full_checks,bb_p1b_host_cheap_misses;
#endif
#ifdef BB_P1B_TEST_HOOK /* host fault injection only; never defined for the target */
void bb_p1b_test_hook(bb_audio *);
#define BB_P1B_BOUNDARY_HOOK(a) bb_p1b_test_hook(a)
#else
#define BB_P1B_BOUNDARY_HOOK(a) ((void)0)
#endif
/* Instruments (CPU0 writes, diagnostics read). bb_p1b_enable=0 restores the
 * per-span path at run time for A/B measurement without a rebuild. */
volatile uint32_t bb_p1b_enable=1;
uint32_t bb_p1b_call_jobs,bb_p1b_joins_saved,bb_p1b_guard_trips,bb_p1b_fallbacks;
uint32_t bb_p1b_guard_first_mask,bb_p1b_guard_discards,bb_p1b_guard_cpu1_faults;
extern int bb_rich_worker_discard(void);
static int bb_p1b_quiet(bb_audio *a) {
    /* audio_before_tick's only writers of B inputs in this configuration: a B
     * pattern or phrase that launches at the next tick. One queued for a later
     * boundary does not touch B at this one (the guard below re-checks). */
    if(bb_audio_b_launch_due(a,a->patterns.position))return 0;
#if BB_SAMPLE_HOST_INTEGRATION
    /* Machines above B must contribute nothing between deferred B segments. */
    for(unsigned k=2;k<BB_MACHINES;k++)if(bb_audio_sample_layer(a,k))return 0;
#endif
    return 1;
}
static void bb_p1b_fingerprint(bb_audio *a,uint32_t *v) {
    unsigned tails=0;
#if BB_SAMPLE_HOST_INTEGRATION
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,1);
    if(binding)for(unsigned s=0;s<binding->count;s++)tails|=binding->slots[s].voice.active;
    v[0]=bb_audio_sample_layer_id(a,1);v[1]=(uint32_t)bb_aux_voices_active(a->aux,1);
#else
    v[0]=v[1]=0;
#endif
    v[2]=tails;v[3]=a->core.bpm;v[4]=a->core.swing;v[5]=a->core.sample_rate;v[6]=a->core.running;
    v[7]=a->core.drums[1].enabled&&!a->core.drums[1].muted;
    v[8]=a->drum_patterns.family_epoch[1];v[9]=a->drum_patterns.family[1];v[10]=a->rich.valid[1];
    v[11]=a->drum_revision[1];v[12]=a->core.drums[1].sound_revision;v[13]=a->rich.active[1].voices;
}
static int bb_p1b_inputs_unchanged(bb_audio *a) {
    uint32_t now[BB_P1B_INPUTS];
    bb_p1b_fingerprint(a,now);
    int same=!memcmp(now,bb_p1b.inputs,sizeof now) &&
        !memcmp(&a->rich.player[1],&bb_p1b.player,sizeof bb_p1b.player) &&
        !memcmp(&a->groups[1],&bb_p1b.groups,sizeof bb_p1b.groups);
#ifdef BB_P1B_HOST_FULL_GUARD
    bb_p1b_host_full_checks++;
    if(same && (memcmp(&a->kit[1],&bb_p1b.kit,sizeof bb_p1b.kit) ||
                memcmp(&a->rich.active[1],&bb_p1b.phrase,sizeof bb_p1b.phrase)))bb_p1b_host_cheap_misses++;
#endif
    return same;
}
/* Which of the guarded inputs differ (diagnostic only, computed on a trip). */
static __attribute__((noinline)) uint32_t bb_p1b_guard_why(bb_audio *a) {
    uint32_t now[BB_P1B_INPUTS],why=0;
    bb_p1b_fingerprint(a,now);
    for(unsigned i=0;i<BB_P1B_INPUTS;i++)if(now[i]!=bb_p1b.inputs[i])why|=1u<<i;
    if(memcmp(&a->rich.player[1],&bb_p1b.player,sizeof bb_p1b.player))why|=1u<<14;
    if(memcmp(&a->groups[1],&bb_p1b.groups,sizeof bb_p1b.groups))why|=1u<<15;
    return why;
}
/* PR #19 review F1. BEHAVIOUR CHANGE, SEPARATE FROM THE TIMING CHANGE (P1b
 * output is unchanged): a guard trip is a P1b prediction fault, not a CPU1
 * fault. The job in flight was sealed from valid inputs, so CPU0 joins it,
 * verifies its completion and seal, and drops its output without adopting
 * B's player, kit, groups or buses. Only this block is rejected (offset not
 * advanced; B reconciled on CPU0 from the job's first frame, exactly like a
 * failed job). CPU1 stays in service: no first-fault capture, no latch, no
 * retirement. CPU1 RETIREMENT SEMANTICS ARE UNCHANGED for real CPU1 faults:
 * if that join times out or the completion/seal does not verify,
 * bb_rich_worker_discard latches it through bb_c1_live_fail and the existing
 * retire path runs at the next block, as before. */
static __attribute__((noinline)) void bb_p1b_guard_trip(uint32_t why) {
    bb_p1b_guard_trips++;
    if(!bb_p1b_guard_first_mask)bb_p1b_guard_first_mask=why|0x80000000u;
    bb_p1b.active=0;bb_p1b.rejected=1;bb_p1b.recover=1;
    if(bb_rich_worker_discard())bb_p1b_guard_discards++;
    else bb_p1b_guard_cpu1_faults++;
}
/* Mix the joined call-level B buses span by span. Same expressions, operand
 * order and ramp as the per-span loop in audio_span_body; only the scratch
 * index is relative to the job's first frame. */
static __attribute__((noinline)) void bb_p1b_mix(bb_audio *a,unsigned k,float pan_l,float pan_r) {
    uint32_t current=a->mix_gain[k];
    for(unsigned s=0;s<bb_p1b.count;s++) {
        const bb_p1b_segment *g=&bb_p1b.seg[s];
        const unsigned offset=g->offset,frames=g->frames,rel=offset-bb_p1b.first;
        const uint32_t target=g->target;const float fixed=g->fixed;
        const float *curve=g->curved?bb_p1b.curve+offset:0;
        const float *dry_bus=a->drum_dry+rel,*rev_bus=a->drum_rev+rel,*dly_bus=a->drum_dly+rel;
        unsigned i;
        if(current==target && !curve && a->scope.source!=BB_SCOPE_KIT_A+k) {
            const float gain=(float)current*(1.0f/65536.0f);
            bb_drum_mix_settled(dry_bus,dly_bus,rev_bus,
                a->left+offset,a->right+offset,
#if BB_WITH_EFFECTS
                a->echo_send+offset,a->spring_send+offset,
#else
                0,0,
#endif
                frames,pan_l,pan_r,gain,fixed);
            continue;
        }
        for(i=0;i<frames;i++) {
            float dry=dry_bus[i],gain;
            if(current<target)current+=target-current>256u?256u:target-current;
            if(current>target)current-=current-target>256u?256u:current-target;
            gain=(float)current*(1.0f/65536.0f);
            float bus=curve?curve[i]:fixed;
            if(a->scope.source==BB_SCOPE_KIT_A+k)
                a->scope.samples[offset+i]=(dry*pan_l*gain*bus+dry*pan_r*gain*bus)*0.5f;
            a->left[offset+i]+=dry*pan_l*gain*bus;
            a->right[offset+i]+=dry*pan_r*gain*bus;
#if BB_WITH_EFFECTS
            a->echo_send[offset+i]+=dly_bus[i]*0.2f*gain*bus;
            a->spring_send[offset+i]+=rev_bus[i]*0.2f*gain*bus;
#endif
        }
    }
    a->mix_gain[k]=current;
}
#endif
#else
#define BB_RICH_WORKER_ENABLED 0
#define BB_P1B_CALL_JOB 0
#endif
#if BB_RICH_WORKER_ENABLED
static __attribute__((noinline)) void audio_recover_span(bb_audio *a,uint32_t frames){
 /* Reconcile within each failed span, before the core advances or applies a
  * tempo/part change. Use CPU0-owned state only; never inspect failed payload.
  * The block remains rejected (offset is NOT advanced). No missed hit replays. */
#if BB_P1B_CALL_JOB
 if(a->core.running && (bb_rich_worker_recovery_pending() || bb_p1b.rejected)){
#else
 if(a->core.running && bb_rich_worker_recovery_pending()){
#endif
  uint64_t end=a->core.frame+frames;
  for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.valid[m]){
   uint64_t first=a->rich.player[m].queue.floor,earliest=a->core.frame;
#if BB_P1B_CALL_JOB
   /* B was never adopted during a failed call-level job: its last accepted
    * state is the job's first frame, still within this one call. */
   if(m==1 && bb_p1b.recover)earliest=bb_p1b.first_frame;
#endif
   if(first==end)continue; /* this owner already completed the span */
   if(first<earliest || first>end || end-first>128){a->rich.render_errors++;continue;}
   bb_rsp_state next;
   __builtin_memcpy(&next,&a->rich.player[m],sizeof next);
   if(!bb_rsp_skip(&a->rich.active[m],&next,a->core.sample_rate,a->core.bpm,
      a->core.swing,(unsigned)(end-first),first,&next)){a->rich.render_errors++;continue;}
   __builtin_memcpy(&a->rich.player[m],&next,sizeof next);
  }
 }
#if BB_P1B_CALL_JOB
 bb_p1b.recover=0;
#endif
}
#endif
/* A disabled B layer with no remaining voice tails still needs its phrase
 * clock advanced, but it has no audio to render or publish to CPU1. */
#if BB_RICH_WORKER_ENABLED
static int bb_rich_b_silent(const bb_audio *a) {
    if(!a->core.running || a->core.drums[1].enabled || !a->rich.valid[1] ||
       a->drum_patterns.family[1] || drum808_active(&a->kit[1]) ||
       bb_aux_voices_active(a->aux,1) ||
       bb_audio_sample_layer_id(a,1)!=BB_SAMPLE_808_ID)return 0;
#if BB_P1B_CALL_JOB
    if(bb_p1b.active)return 0; /* an earlier span already owns this call */
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    bb_sample_audio_binding *binding=bb_audio_sample_layer(a,1);
    if(binding)for(unsigned s=0;s<binding->count;s++)
        if(binding->slots[s].voice.active)return 0;
#endif
    return 1;
}
#endif
static void audio_span_body(void *,uint32_t);
static void audio_span(void *ctx,uint32_t frames){
#if BB_P1B_CALL_JOB
 bb_p1b.carry=0;bb_p1b.recover=0;
#endif
 audio_span_body(ctx,frames);
#if BB_RICH_WORKER_ENABLED
#if BB_P1B_CALL_JOB
 if(bb_p1b.carry)return; /* published for the whole call; joined by its last span */
 if(bb_p1b.active){bb_p1b.active=0;bb_p1b.recover=1;} /* early exit with the job out */
#endif
 bb_rich_worker_cleanup();
#if BB_P1B_CALL_JOB
 if(bb_rich_worker_recovery_pending() || bb_p1b.rejected)audio_recover_span(ctx,frames);
#else
 if(bb_rich_worker_recovery_pending())audio_recover_span(ctx,frames);
#endif
#endif
}
static void audio_span_body(void *ctx,uint32_t frames) {
    bb_audio *a=ctx;unsigned i,k;
    if(!one_drum_audio_ok(a))return;
    float fixed;const float *curve;
    /* Commands/events only run between spans. The embedded compiler did not
     * hoist these invariants across writes into sibling sample arrays.
     * Preserve operation order: no reciprocal substitution or gain regrouping. */
    const unsigned offset=a->offset;
#if BB_RICH_WORKER_ENABLED
    const int silent_b=bb_rich_b_silent(a);
    int rich_worker=-1;
#if BB_P1B_CALL_JOB
    int p1b_last=0;
    if(bb_p1b.rejected)return; /* a guard trip already rejected this call */
    if(bb_p1b.active) {
        /* Later span of a call-level job: no new publication or join here. */
        BB_P1B_BOUNDARY_HOOK(a);
        if(!bb_p1b.count || bb_p1b.count>=BB_P1B_SEGMENTS || offset!=bb_p1b.seg[bb_p1b.count-1u].offset+bb_p1b.seg[bb_p1b.count-1u].frames ||
           offset+frames>bb_p1b.end){bb_p1b_guard_trip(1u<<16);return;}
        if(!bb_p1b_inputs_unchanged(a)){bb_p1b_guard_trip(bb_p1b_guard_why(a));return;}
        rich_worker=1;p1b_last=offset+frames==bb_p1b.end;
    } else
#endif
    /* Only native B without outgoing sample/aux tails. All other backends
     * retain their exact serial route, not dropped sounds or changed kits. */
    /* STOP retains native tails; keep their existing worker ownership. */
    if(!silent_b&&(a->core.running||drum808_active(&a->kit[1]))&&a->rich.valid[1]&&!a->drum_patterns.family[1]&&
       !bb_aux_voices_active(a->aux,1)&&
       bb_audio_sample_layer_id(a,1)==BB_SAMPLE_808_ID){
        bb_sample_audio_binding *binding=bb_audio_sample_layer(a,1);unsigned tails=0;
        if(binding)for(unsigned s=0;s<binding->count;s++)tails|=binding->slots[s].voice.active;
#if BB_P1B_CALL_JOB
        const unsigned remaining=bb_p1b.call_frames>offset?bb_p1b.call_frames-offset:0u;
        if(!tails && bb_p1b_enable && a->core.running && frames<remaining && remaining<=128u && bb_p1b_quiet(a)) {
            /* CPU0 copies first: the job may complete before begin returns. */
            bb_p1b_fingerprint(a,bb_p1b.inputs);
            __builtin_memcpy(&bb_p1b.player,&a->rich.player[1],sizeof bb_p1b.player);
            __builtin_memcpy(&bb_p1b.groups,&a->groups[1],sizeof bb_p1b.groups);
#ifdef BB_P1B_HOST_FULL_GUARD
            __builtin_memcpy(&bb_p1b.kit,&a->kit[1],sizeof bb_p1b.kit);
            __builtin_memcpy(&bb_p1b.phrase,&a->rich.active[1],sizeof bb_p1b.phrase);
#endif
            rich_worker=bb_rich_worker_begin(&a->rich.active[1],&a->rich.player[1],
                &a->kit[1],&a->groups[1],1,remaining,a->core.frame,a->core.sample_rate,a->core.bpm,
                a->core.swing,a->core.running,a->core.drums[1].enabled&&!a->core.drums[1].muted,
                a->drum_patterns.family_epoch[1]);
            if(rich_worker==-2)return;
            if(rich_worker==1) {
                bb_p1b.active=1;bb_p1b.first=offset;bb_p1b.end=offset+remaining;bb_p1b.count=0;
                bb_p1b.first_frame=a->core.frame;bb_p1b_call_jobs++;
            } else bb_p1b_fallbacks++; /* plan refused before publication: per-span path below */
        }
        if(!bb_p1b.active)
#endif
        if(!tails)rich_worker=bb_rich_worker_begin(&a->rich.active[1],&a->rich.player[1],
            &a->kit[1],&a->groups[1],1,frames,a->core.frame,a->core.sample_rate,a->core.bpm,
            a->core.swing,a->core.running,a->core.drums[1].enabled&&!a->core.drums[1].muted,
            a->drum_patterns.family_epoch[1]);
        if(rich_worker==-2)return;
    }
#endif
#if BB_AUDIO_PROFILE
    bb_attr_begin(&bb_attr,BB_ATTR_PREPARE,bb_profile_clock());
#endif
#if BB_OWNER_AUDIO_BRIDGE
    if(!bb_owner_audio_span_begin(a,frames))return;
    int worker=-1;const int retained=0;
#else
#if BB_WITH_909_ROUTE
    const int nine=bb909_audio_layer();
    bb909_audio_view nine_bus;
    if(nine>=0 && !bb909_audio_begin(frames,&a->groups[nine]))return;
    int worker=nine>=0?nine:bb_retained_begin(a->kit,a->groups,BB_MACHINES,frames);
    const int retained=worker>=0 && nine<0;
#else
    const int worker=-1,retained=0; /* one drum and Dillo owned by CPU0 */
#endif
#endif /* legacy worker selection */
#if BB_AUDIO_PROFILE
    bb_attr_end(&bb_attr,BB_ATTR_PREPARE,bb_profile_clock(),frames);
#endif
    /* Group-aware worker and CPU0 overlap paths are supplied by this harness. */
    if(worker==-2)return; /* failed private job: reject, never replay */
    const unsigned original_frames=frames;
    for(unsigned slice_offset=0;slice_offset<original_frames;) {
    const unsigned frames=(retained || BB_OWNER_AUDIO_BRIDGE) && original_frames-slice_offset>64u?64u:original_frames-slice_offset;
    const unsigned offset=a->offset+slice_offset;
#if BB_WITH_DILLO
    BB_PROFILE_BEGIN(BB_PROF_DILLO);
#if BB_DILLO_RENDER_QUANTUM == 32
    /* Preserve the installed physical subdivisions across larger callbacks. */
    for(unsigned rendered=0;rendered<frames;) {
        unsigned n=32u-((offset+rendered)&31u);
        if(n>frames-rendered)n=frames-rendered;
        bb_dillo_render(&a->dillo,a->bass+slice_offset+rendered,n);
        rendered+=n;
    }
#else
    bb_dillo_render(&a->dillo,a->bass+slice_offset,frames);
#endif
    curve=audio_band_fader(a,BB_MIX_DILLO,frames,&fixed);
#if BB_WITH_EFFECTS
    const float dillo_echo=0.5f*(float)a->core.effects.value[BB_FX_DILLO_ECHO]/127.0f;
    const float dillo_spring=0.5f*(float)a->core.effects.value[BB_FX_DILLO_SPRING]/127.0f;
#endif
    for(i=0;i<frames;i++) {
        float gain=curve?curve[i]:fixed;
        float sample=a->bass[slice_offset+i],dry=sample*0.5f*gain;
        if(a->scope.source==BB_SCOPE_DILLO)a->scope.samples[offset+i]=dry;
        a->left[offset+i]+=dry;
        a->right[offset+i]+=dry;
#if BB_WITH_EFFECTS
        a->echo_send[offset+i]+=sample*dillo_echo*gain;
        a->spring_send[offset+i]+=sample*dillo_spring*gain;
#endif
    }
    BB_PROFILE_END(BB_PROF_DILLO,frames);
#endif
#if BB_WITH_TELEGRAM
    BB_PROFILE_BEGIN(BB_PROF_TELEGRAM);
    bb_telegram_render(&a->telegram,a->lead+slice_offset,frames);
    curve=audio_band_fader(a,BB_MIX_TELEGRAM,frames,&fixed);
#if BB_WITH_EFFECTS
    const float telegram_echo=0.65f*(float)a->core.effects.value[BB_FX_TELEGRAM_ECHO]/127.0f;
    const float telegram_spring=0.65f*(float)a->core.effects.value[BB_FX_TELEGRAM_SPRING]/127.0f;
#endif
    for(i=0;i<frames;i++) {
        float gain=curve?curve[i]:fixed;
        float sample=a->lead[slice_offset+i],dry=sample*0.65f*gain;
        if(a->scope.source==BB_SCOPE_TELEGRAM)a->scope.samples[offset+i]=dry;
        a->left[offset+i]+=dry;
        a->right[offset+i]+=dry;
#if BB_WITH_EFFECTS
        a->echo_send[offset+i]+=sample*telegram_echo*gain;
        a->spring_send[offset+i]+=sample*telegram_spring*gain;
#endif
    }
    BB_PROFILE_END(BB_PROF_TELEGRAM,frames);
#endif
    if(retained) {
#if BB_AUDIO_PROFILE
        bb_attr_begin(&bb_attr,BB_ATTR_OTHER_KIT,bb_profile_clock());
#endif
        int prepared=bb_retained_prepare_others(a->kit,a->groups,BB_MACHINES,
            original_frames,slice_offset,frames,(unsigned)worker);
#if BB_AUDIO_PROFILE
        bb_attr_end(&bb_attr,BB_ATTR_OTHER_KIT,bb_profile_clock(),frames);
#endif
        if(!prepared)return;
    }
    slice_offset+=frames;
    if(retained && slice_offset<original_frames) {
#if BB_AUDIO_PROFILE
        bb_attr_begin(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock());
#endif
        int advanced=bb_retained_advance((unsigned)worker,original_frames);
#if BB_AUDIO_PROFILE
        bb_attr_end(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock(),0);
#endif
        if(!advanced)return;
    }
    }
    /* No triggers or queued controls occur inside a sequencer event span.
     * Hoist channel constants and avoid walking all 13 lanes of an entirely
     * idle kit for every sample. Muted/disabled kits with tails still render.
     * Keep the active engine's one-sample semantics: bulk upstream rendering
     * is not block-transparent for shared metal banks and tail shutdown. */
    BB_PROFILE_BEGIN(BB_PROF_DRUMS);
    /* Render the other CPU0-owned kits before joining the private worker.
     * All faders, pans, sends and additions below retain their original order. */
    if(worker>=0 && !retained && !bb_parallel_prepare_others(a->kit,a->groups,BB_MACHINES,frames,(unsigned)worker))return;
#if BB_OWNER_AUDIO_BRIDGE
    int local_cached=-1;
    for(unsigned local=0;local<BB_MACHINES;local++) {
        int available=bb_owner_audio_local(a,local);
        if(available<0)return;
        if(!available||!drum808_active(&a->kit[local]))continue;
        local_cached=(int)local;
        memset(a->drum_dry,0,frames*sizeof(float));
        memset(a->drum_rev,0,frames*sizeof(float));
        memset(a->drum_dly,0,frames*sizeof(float));
#if BB_AUDIO_PROFILE
        /* Attribution v2 slot1: CPU0-owned synthesis only. */
        bb_attr_begin(&bb_attr,BB_ATTR_OTHER_KIT,bb_profile_clock());
#endif
        bb_proposal_group_span(&a->kit[local],&a->groups[local],
            a->drum_dry,a->drum_rev,a->drum_dly,(int)frames);
#if BB_AUDIO_PROFILE
        bb_attr_end(&bb_attr,BB_ATTR_OTHER_KIT,bb_profile_clock(),frames);
#endif
        break;
    }
#if BB_AUDIO_PROFILE
    /* Attribution v2 slot2: join plus checked view adoption/validation.
     * Close the diagnostic scope on failure as well as success. */
    bb_attr_begin(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock());
#endif
    int collected=bb_owner_audio_span_collect(a,frames);
#if BB_AUDIO_PROFILE
    bb_attr_end(&bb_attr,BB_ATTR_ADVANCE,bb_profile_clock(),frames);
#endif
    if(!collected)return;
#endif
    curve=audio_band_fader(a,BB_MIX_DRUMS,frames,&fixed);
    for(k=0;k<BB_MACHINES;k++) {
#if BB_OWNER_AUDIO_BRIDGE
        int handled=bb_owner_audio_mix(a,k,curve,fixed,frames);
        if(handled<0)return;
        if(handled>0)continue;
#endif
        uint32_t target=((uint32_t)a->core.drums[k].level*65536u+63u)/127u;
        uint32_t current=a->mix_gain[k];
        float pan_l=(k&1u)?0.16f:0.24f,pan_r=(k&1u)?0.24f:0.16f;
#if BB_RICH_WORKER_ENABLED
        if(k==1 && silent_b) {
            bb_rsp_state next;
            __builtin_memcpy(&next,&a->rich.player[1],sizeof next);
            if(!bb_rsp_skip(&a->rich.active[1],&next,a->core.sample_rate,
                            a->core.bpm,a->core.swing,frames,a->core.frame,&next)){
                a->rich.render_errors++;return;
            }
            __builtin_memcpy(&a->rich.player[1],&next,sizeof next);
            gg_advance(&a->groups[1],frames);
            uint32_t distance=frames*256u;
            if(current<target)current+=target-current>distance?distance:target-current;
            if(current>target)current-=current-target>distance?distance:current-target;
            a->mix_gain[1]=current;
            if(a->scope.source==BB_SCOPE_KIT_A+1)
                memset(a->scope.samples+offset,0,frames*sizeof(float));
            continue;
        }
#endif
#if BB_WITH_909_ROUTE
        if((int)k==nine) {
            if(!bb909_audio_finish(frames,&nine_bus))return;
            for(i=0;i<frames;i++) {
                if(current<target)current+=target-current>256u?256u:target-current;
                if(current>target)current-=current-target>256u?256u:current-target;
                float gain=(float)current*(1.0f/65536.0f);
                float bus=curve?curve[i]:fixed;
                /* Native per-voice stereo placement, unity centre. Match
                 * the existing drum layer's centre sum at 0.2 per side;
                 * do not fold stereo back to mono or alter effects state. */
                float left=nine_bus.left[i]*0.2f*gain*bus;
                float right=nine_bus.right[i]*0.2f*gain*bus;
                if(a->scope.source==BB_SCOPE_KIT_A+k)a->scope.samples[offset+i]=(left+right)*0.5f;
                a->left[offset+i]+=left;a->right[offset+i]+=right;
#if BB_WITH_EFFECTS
                a->echo_send[offset+i]+=nine_bus.echo[i]*0.2f*gain*bus;
                a->spring_send[offset+i]+=nine_bus.spring[i]*0.2f*gain*bus;
#endif
            }
            a->mix_gain[k]=current;
            if(!bb909_audio_release(&a->groups[k]))return;
            continue;
        }
#endif
        if(!(
#if BB_WITH_RICH_DRUM_RENDER
            (k<BB_RICH_DRUMS&&a->rich.valid[k])||
#endif
#if BB_SAMPLE_HOST_INTEGRATION
            bb_audio_sample_layer(a,k)||
#endif
#if BB_OWNER_AUDIO_BRIDGE
            (int)k==local_cached||
#endif
            (worker>=0?bb_parallel_cached_active(k):drum808_active(&a->kit[k])))) {
            gg_advance(&a->groups[k],frames);
            uint32_t distance=frames*256u;
            if(current<target)current+=target-current>distance?distance:target-current;
            if(current>target)current-=current-target>distance?distance:current-target;
            a->mix_gain[k]=current;
            continue;
        }
#if BB_USE_DRUM_SPAN
#if BB_WITH_RICH_DRUM_RENDER
        if(k<BB_RICH_DRUMS&&a->rich.valid[k]){
#if BB_RICH_WORKER_ENABLED
            if((int)k==rich_worker){
#if BB_P1B_CALL_JOB
                if(bb_p1b.active) {
                    bb_p1b_segment *g=&bb_p1b.seg[bb_p1b.count++];
                    g->offset=offset;g->frames=frames;g->target=target;g->fixed=fixed;g->curved=curve!=0;
                    if(curve)__builtin_memcpy(bb_p1b.curve+offset,curve,frames*sizeof(float));
                    if(!p1b_last)continue; /* B mixes after its join, in span order */
                    if(!bb_rich_worker_finish(k,bb_p1b.end-bb_p1b.first,a->drum_patterns.family_epoch[k],&a->rich.player[k],
                        &a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,&a->hits[k]))return;
                    bb_p1b.active=0;bb_p1b_joins_saved+=bb_p1b.count-1u;
                    bb_p1b_mix(a,k,pan_l,pan_r);
                    continue;
                }
#endif
                if(!bb_rich_worker_finish(k,frames,a->drum_patterns.family_epoch[k],&a->rich.player[k],
                    &a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,&a->hits[k]))return;
            }else
#endif
            {
            memset(a->drum_dry,0,frames*sizeof(float));
            memset(a->drum_rev,0,frames*sizeof(float));
            memset(a->drum_dly,0,frames*sizeof(float));
            if(!bb_audio_phrase_render(a,k,&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,
                frames,a->core.frame))return;
            }
        }else
#endif
#if BB_SAMPLE_HOST_INTEGRATION
        if(bb_audio_sample_layer(a,k)) {
            bb_sample_audio_binding *s=bb_audio_sample_layer(a,k);
#if defined(BB_SAMPLE_HYBRID_TRIAL)
            /* Both backends retain tails across a route change. Advance the
             * shared faders ONCE: synth uses a snapshot; samples own progression.
             * No extra output buffers, no sample copy and no voice reset. */
            if(drum808_active(&a->kit[k])){
                gg_state synth_groups=a->groups[k];
                memset(a->drum_dry,0,frames*sizeof(float));
                memset(a->drum_dly,0,frames*sizeof(float));
                memset(a->drum_rev,0,frames*sizeof(float));
                bb_sample_profile_synth(k,&a->kit[k],&synth_groups,a->drum_dry,a->drum_rev,a->drum_dly,(int)frames);
                if(bb_sample_profile_buses(s->slots,s->count,&a->groups[k],s->gain,s->echo,s->spring,
                    a->drum_dry,a->drum_dly,a->drum_rev,frames,1)!=BB_SK_OK){a->sample_rejected++;return;}
            }else
#endif
            if(bb_sample_profile_buses(s->slots,s->count,&a->groups[k],s->gain,s->echo,s->spring,
                a->drum_dry,a->drum_dly,a->drum_rev,frames,0)!=BB_SK_OK){a->sample_rejected++;return;}
        } else
#endif
        if((int)k==worker) {
            if(!(retained?bb_retained_finish(k,&a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,frames):
                  bb_parallel_finish(k,&a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,frames)))return;
        } else if(worker>=0) {
            if(!bb_parallel_cached_other(k,a->drum_dry,a->drum_rev,a->drum_dly,frames))return;
        } else
#if BB_OWNER_AUDIO_BRIDGE
        if((int)k!=local_cached)
#endif
        {
            memset(a->drum_dry,0,frames*sizeof(float));
            memset(a->drum_rev,0,frames*sizeof(float));
            memset(a->drum_dly,0,frames*sizeof(float));
#if BB_SAMPLE_HOST_INTEGRATION
            bb_sample_profile_synth(k,&a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,(int)frames);
#else
            bb_proposal_group_span(&a->kit[k],&a->groups[k],a->drum_dry,a->drum_rev,a->drum_dly,(int)frames);
#endif
        }
#endif
#if BB_USE_DRUM_SPAN
        if(current==target && !curve && a->scope.source!=BB_SCOPE_KIT_A+k) {
            const float gain=(float)current*(1.0f/65536.0f);
            bb_drum_mix_settled(a->drum_dry,a->drum_dly,a->drum_rev,
                a->left+offset,a->right+offset,
#if BB_WITH_EFFECTS
                a->echo_send+offset,a->spring_send+offset,
#else
                0,0,
#endif
                frames,pan_l,pan_r,gain,fixed);
            a->mix_gain[k]=current;
            continue;
        }
#endif
        for(i=0;i<frames;i++) {
#if BB_USE_DRUM_SPAN
        float dry=a->drum_dry[i],gain;
#else
        float dry=0,rev=0,dly=0,gain;
#endif
        /* Maximum full-scale transition: 256 samples (about 5.8 ms).
         * Never restart the ramp at a caller-block boundary. Voices keep
         * their state at silence, so lifting a fader reveals their tails. */
        if(current<target)current+=target-current>256u?256u:target-current;
        if(current>target)current-=current-target>256u?256u:current-target;
        gain=(float)current*(1.0f/65536.0f);
#if !BB_USE_DRUM_SPAN
        drum808_render(&a->kit[k],&dry,&rev,&dly,1);
#endif
        float bus=curve?curve[i]:fixed;
        /* Mono midpoint of this kit's dry stereo contribution. */
        if(a->scope.source==BB_SCOPE_KIT_A+k)
            a->scope.samples[offset+i]=(dry*pan_l*gain*bus+dry*pan_r*gain*bus)*0.5f;
        a->left[offset+i]+=dry*pan_l*gain*bus;
        a->right[offset+i]+=dry*pan_r*gain*bus;
#if BB_WITH_EFFECTS
#if BB_USE_DRUM_SPAN
        a->echo_send[offset+i]+=a->drum_dly[i]*0.2f*gain*bus;
        a->spring_send[offset+i]+=a->drum_rev[i]*0.2f*gain*bus;
#else
        a->echo_send[offset+i]+=dly*0.2f*gain*bus;
        a->spring_send[offset+i]+=rev*0.2f*gain*bus;
#endif
#endif
        }
        a->mix_gain[k]=current;
    }
#if BB_OWNER_AUDIO_BRIDGE
    if(!bb_owner_audio_span_release(a))return;
#endif
    BB_PROFILE_END(BB_PROF_DRUMS,frames);
    a->offset+=frames;a->spans++;
#if BB_P1B_CALL_JOB
    bb_p1b.carry=bb_p1b.active;
#endif
}
#if BB_WITH_DILLO || BB_WITH_TELEGRAM
static void audio_note(void *ctx,const bb_note_event *e) {
    bb_audio *a=ctx;bb_core *c=&a->core;
    bb_note played;
    uint32_t denom,inc,frames;
    if(a->keys[e->synth].count)return; /* clock continues; only voice ownership changes */
    played=bb_note_transposed(e->note,a->transpose[e->synth]);
    denom=bb_swing_denominator(c->sample_rate,c->active_swing,c->swing_odd);
    inc=(uint32_t)c->bpm*400u;frames=(denom-c->phase+inc-1u)/inc;
#if BB_WITH_DILLO
    if(e->synth==0) {
        bb_sequence *s=&c->synths[0];
#if BB_WITH_PATTERN
        if(!bb_dillo_step_expression(&a->dillo,played,frames,s->notes[(e->step+1u)%s->length].gate,
                                    a->patterns.expression[0][e->step])) {
            a->patterns.expression_errors++;bb_dillo_stop(&a->dillo);
        }
#else
        bb_dillo_step(&a->dillo,played,frames,s->notes[(e->step+1u)%s->length].gate);
#endif
    }
#endif
#if BB_WITH_TELEGRAM
    if(e->synth==1)bb_telegram_step(&a->telegram,played,frames);
#endif
}
#endif
/* The old path processes one complete block. A live part change first finishes
 * its preceding prefix with the old effect settings, then changes parameters.
 * Thus earlier audio cannot receive settings from the following part. */
#if BB_WITH_EFFECTS
static int audio_effect_settings(bb_audio *a) {
    if(memcmp(&a->applied_effects,&a->core.effects,sizeof a->applied_effects)) {
        const uint16_t *p=a->core.effects.value;
        if(!bb_echo_set(&a->echo,p[BB_FX_ECHO_MS],p[BB_FX_ECHO_FEEDBACK],p[BB_FX_ECHO_TONE],
                        p[BB_FX_ECHO_DRIVE],p[BB_FX_ECHO_WEAR],p[BB_FX_ECHO_WET],p[BB_FX_ECHO_HEADS]) ||
           !bb_spring_set(&a->spring,p[BB_FX_SPRING_SIZE],p[BB_FX_SPRING_DAMP],p[BB_FX_SPRING_WET]) ||
           !bb_echo_motion_set(&a->echo,p[BB_FX_ECHO_MOTION]))return 0;
        a->applied_effects=a->core.effects;
    }
    return 1;
}
static int audio_wet(bb_audio *a,unsigned first,unsigned frames) {
    if(!frames)return 1;
    BB_PROFILE_BEGIN(BB_PROF_ECHO);
    int echo_ok=bb_echo_process(&a->echo,a->echo_send+first,a->wet_left+first,a->wet_right+first,frames);
    BB_PROFILE_END(BB_PROF_ECHO,frames);
    if(!echo_ok)return 0;
    for(unsigned i=first;i<first+frames;i++) {
        a->left[i]+=a->wet_left[i];a->right[i]+=a->wet_right[i];
    }
    BB_PROFILE_BEGIN(BB_PROF_SPRING);
    int spring_ok=bb_spring_process(&a->spring,a->spring_send+first,a->wet_left+first,frames);
    BB_PROFILE_END(BB_PROF_SPRING,frames);
    if(!spring_ok)return 0;
    for(unsigned i=first;i<first+frames;i++) {
        a->left[i]+=a->wet_left[i];a->right[i]+=a->wet_left[i];
    }
    return 1;
}
#endif
#if BB_WITH_PARTS
static void audio_part_changed(bb_audio *a) {
#if BB_WITH_PATTERN
    bb_audio_pattern_cancel(a,1);
#endif
#if BB_WITH_EFFECTS
    if(!audio_wet(a,a->wet_offset,a->offset-a->wet_offset) || !audio_effect_settings(a))
        a->parts_error=1;
    a->wet_offset=a->offset;
#endif
    for(unsigned k=0;k<BB_MACHINES;k++)audio_sync_drum(a,k);
#if BB_WITH_DILLO
    if(!a->keys[0].count)bb_dillo_stop(&a->dillo);
    if(memcmp(&a->dillo.sound,&a->core.dillo,sizeof a->core.dillo))bb_dillo_sound(&a->dillo,&a->core.dillo);
    if(a->core.synths[0].muted)audio_release_synth(a,0);
    else if(a->keys[0].count)audio_key_voice(a,0,1);
#endif
#if BB_WITH_TELEGRAM
    if(!a->keys[1].count)bb_telegram_stop(&a->telegram);
    if(memcmp(&a->telegram.sound,&a->core.telegram,sizeof a->core.telegram))bb_telegram_sound(&a->telegram,&a->core.telegram);
    if(a->core.synths[1].muted)audio_release_synth(a,1);
    else if(a->keys[1].count)audio_key_voice(a,1,1);
#endif
#if BB_WITH_EVOLVE
    memset(&a->evolve,0,sizeof a->evolve);
    a->evolve_home_pending=0;a->evolve_result=BB_EV_RESULT_NONE;
#endif
}
static void audio_part_command(bb_audio *a,const bb_command *c) {
#if BB_WITH_PREPARED_BANK
    if(!a->parts.parts.ready){
        a->parts_result=(uint8_t)pb_seed_request(&a->parts,&a->core,16,bb_audio_families(a),c->value);
        if(a->parts_result==BB_PART_REJECTED)return;
    } else {
        if(!pb_bind_families(&a->parts,bb_audio_families(a))){a->parts_result=BB_PART_REJECTED;return;}
        a->parts_result=(uint8_t)pb_part_request(&a->parts,c->value);
    }
#else
    if(!a->parts.ready && !bb_parts_init_families(&a->parts,&a->core,16,bb_audio_families(a))) {
        a->parts_result=BB_PART_REJECTED;return;
    }
    if(!bb_parts_bind_families(&a->parts,bb_audio_families(a))){a->parts_result=BB_PART_REJECTED;return;}
    a->parts_result=(uint8_t)bb_parts_request(&a->parts,&a->core,c->value);
#endif
    if(a->parts_result!=BB_PART_REJECTED)bb_audio_trio_cancel(a);
#if BB_WITH_PATTERN
    if(a->parts_result!=BB_PART_REJECTED)bb_audio_pattern_cancel(a,0);
#endif
    if(a->parts_result==BB_PART_APPLIED)audio_part_changed(a);
}
#endif
#if BB_WITH_PARTS || BB_WITH_EVOLVE || BB_WITH_PATTERN
static void audio_before_tick(void *ctx,bb_core *core) {
#if BB_WITH_PARTS
    bb_audio *a=ctx;
    if(bb_audio_trio_tick(a)==BB_TRIO_APPLIED) {
        a->parts_result=BB_PART_APPLIED;audio_part_changed(a);
    }
#if BB_WITH_PREPARED_BANK
    if(a->parts.parts.ready || a->parts.bank.state==PB_SEED || a->parts.bank.state==PB_SEED_WRITING) {
        if(!pb_bind_families(&a->parts,bb_audio_families(a))){a->parts_result=BB_PART_REJECTED;return;}
        int result=pb_before_tick(&a->parts,core);
        if(result==BB_PART_APPLIED || result==BB_PART_REJECTED)a->parts_result=(uint8_t)result;
        if(result==BB_PART_APPLIED)audio_part_changed(a);
    }
#else
    if(a->parts.ready) {
        if(!bb_parts_bind_families(&a->parts,bb_audio_families(a))){a->parts_result=BB_PART_REJECTED;return;}
        int result=bb_parts_before_tick(&a->parts,core);
        if(result==BB_PART_APPLIED || result==BB_PART_REJECTED)a->parts_result=(uint8_t)result;
        if(result==BB_PART_APPLIED)audio_part_changed(a);
    }
#endif
#endif
#if BB_WITH_PATTERN
    (void)core;
    /* Instrument only: classify this boundary before the ticks consume it. */
    bb_audio_boundary_flags|=BB_BOUNDARY_SEEN|
        (bb_audio_b_launch_due((bb_audio *)ctx,((bb_audio *)ctx)->patterns.position)?BB_BOUNDARY_B_LAUNCH:0u);
    bb_audio_pattern_tick(ctx);
#if BB_WITH_RICH_DRUM_HANDOFF
    bb_audio_phrase_tick((bb_audio *)ctx,((bb_audio *)ctx)->patterns.position?
        ((bb_audio *)ctx)->patterns.position-1u:191u);
#endif
    /* Apply required modes before this boundary's first drum trigger. */
    for(unsigned k=0;k<BB_MACHINES;k++)audio_sync_drum((bb_audio *)ctx,k);
#endif
#if BB_WITH_EVOLVE
    bb_audio_evolve_tick(ctx,core);
#endif
}
#endif
#include "bb_wire.h"
#include "group_storage_binding.inc"
int bb_audio_init(bb_audio *a,bb_enter_fn enter,bb_leave_fn leave,void *ctx) {
    unsigned k;
    if(!a || (!enter != !leave))return 0;
    memset(a,0,sizeof *a);
    if(!bb_init(&a->core,44100) || !bb_commands_init(&a->commands,enter,leave,ctx))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    a->commands.admit=bb_audio_phrase_admit;a->commands.admit_context=a;
#endif
#if BB_WITH_PARTS && BB_WITH_PREPARED_BANK
    pb_init_empty(&a->parts);
#endif
    for(k=0;k<BB_MIX_CHANNELS;k++)a->band_gain[k]=65536u;
#if BB_WITH_EFFECTS
    if(!bb_echo_init(&a->echo,44100) || !bb_spring_init(&a->spring,44100))return 0;
    a->applied_effects=a->core.effects;
#endif
#if BB_OUTPUT_GUARD
    if(!bb_output_init(&a->output,44100,1.412537545f))return 0;
#endif
    for(k=0;k<BB_MACHINES;k++) {
        drum808_init(&a->kit[k]);a->mix_gain[k]=65536u;a->drum_revision[k]=a->core.drums[k].sound_revision;
    }
#if BB_WITH_DILLO
    bb_dillo_init(&a->dillo);bb_dillo_sound(&a->dillo,&a->core.dillo);
#endif
#if BB_WITH_TELEGRAM
    bb_telegram_init(&a->telegram);bb_telegram_sound(&a->telegram,&a->core.telegram);
#endif
#if BB_WITH_PATTERN
    a->patterns.ready=1;
#endif
    return 1;
}
/* ---- D1 (reliability r1): CPU0 fault drain ------------------------------
 * Used only while a latched worker fault cannot be retired (output off until
 * reboot, CPU1 verified held). Nothing here renders, ticks or touches CPU1. */
#if BB_WITH_PATTERN
static uint32_t fault_ticket(const bb_command *c) {
    return (uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24);
}
#endif
static void audio_fault_terminate(bb_audio *a) {
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
    bb_audio_phrase_fault(a);
#endif
#if BB_WITH_PATTERN
    for(unsigned k=0;k<BB_MACHINES;k++)if(a->drum_patterns.pending[k].ready) {
        a->drum_patterns.pending[k].ready=0;a->drum_patterns.status[k].state.queued=0;
        a->drum_patterns.status[k].state.result=BB_PATTERN_REJECTED;
        a->drum_patterns.status[k].error=BB_DRUM_PATTERN_ERROR_FAULT;
    }
    for(unsigned s=0;s<BB_SYNTHS;s++)if(a->patterns.pending[s].ready) {
        a->patterns.pending[s].ready=0;a->patterns.status[s].queued=0;
        a->patterns.status[s].result=BB_PATTERN_REJECTED;
    }
#endif
}
static void audio_fault_command(void *ctx,const bb_command *c) {
    bb_audio *a=ctx;
    switch(c->type) {
    case BB_CMD_TRANSPORT:audio_fault_terminate(a);return; /* only STOP reaches here */
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
    case BB_CMD_DRUM_PHRASE:
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_KIT_PHRASE:
#endif
        bb_audio_phrase_fault_command(a,c);return;
#endif
#if BB_WITH_PATTERN
    case BB_CMD_SYNTH_PATTERN: {
        uint32_t ticket=fault_ticket(c);
        if(c->target>=BB_SYNTHS || c->step>=BB_PATTERN_SLOTS || !ticket)return;
        bb_pattern_slot *slot=&a->patterns.inbox[c->step];
        if(slot->state==BB_PATTERN_SLOT_QUEUED && slot->ticket==ticket && slot->synth==c->target)
            slot->state=BB_PATTERN_SLOT_FREE;
        a->patterns.status[c->target].seen=ticket;a->patterns.status[c->target].result=BB_PATTERN_REJECTED;
        return;
    }
    case BB_CMD_DRUM_PATTERN: {
        uint32_t ticket=fault_ticket(c);
        if(c->target>=BB_MACHINES || c->step>=BB_PATTERN_SLOTS || !ticket)return;
        bb_drum_pattern_slot *slot=&a->drum_patterns.inbox[c->step];
        if(slot->state==BB_PATTERN_SLOT_QUEUED && slot->ticket==ticket && slot->machine==c->target)
            slot->state=BB_PATTERN_SLOT_FREE;
        bb_drum_pattern_status *report=&a->drum_patterns.status[c->target];
        report->state.seen=ticket;report->state.result=BB_PATTERN_REJECTED;report->error=BB_DRUM_PATTERN_ERROR_FAULT;
        return;
    }
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_SAMPLE_KIT:
        /* Complete the picker's serial as a refusal; keep the current binding. */
#if defined(BB_SAMPLE_DUAL_TRIAL)
        if(c->target==1) {
            a->sample_b_requested=(uint8_t)c->value;a->sample_b_select_result=2;
            a->sample_b_select_serial++;return;
        }
#endif
        a->sample_requested=(uint8_t)c->value;a->sample_select_result=2;a->sample_select_serial++;return;
#endif
    case BB_CMD_GROUP_LEVEL:case BB_CMD_GROUP_MUTE:case BB_CMD_SYNTH_TRANSPOSE:case BB_CMD_NOTE:
        audio_live_command(a,c);return; /* CPU0-owned mix/edit state that a save records */
    default:return; /* keys, triggers, evolve, launches: nothing to sound */
    }
}
unsigned bb_audio_fault_drain(bb_audio *a) {
    if(!a || !a->groups)return 0;
    audio_fault_terminate(a);
    return bb_apply_pending_faulted(&a->core,&a->commands,audio_fault_command,a);
}
int bb_audio_render(bb_audio *a,unsigned frames) {
    if(!a || !frames || frames>128)return 0;
    if(!one_drum_audio_ok(a)) {
        memset(a->left,0,frames*sizeof(float));memset(a->right,0,frames*sizeof(float));
#if !BB_OWNER_AUDIO_BRIDGE
        /* D3 (reliability r1): this precondition can be broken by an accepted
         * command (e.g. DRUM_ENABLE on layer 2/3, SYNTH_MUTE 1 off) and only a
         * later command can repair it. Keep the command plane alive: drain
         * the entry snapshot (no tick, no render) so STOP, repairs and loads
         * still reach a terminal state. Normal blocks never take this path. */
        if(a->groups)(void)bb_apply_pending_live(&a->core,&a->commands,audio_live_command,a);
#endif
        return 0;
    }
    if(!a->groups){
        memset(a->left,0,frames*sizeof(float));memset(a->right,0,frames*sizeof(float));
        return 0; /* Platform must not enable audio before storage is bound. */
    }
#if BB_WITH_RICH_DRUM_RENDER
    const uint32_t render_errors_before=a->rich.render_errors;
#endif
    BB_PROFILE_BLOCK_BEGIN();
    memset(a->left,0,frames*sizeof(float));memset(a->right,0,frames*sizeof(float));
    if(a->scope.source)memset(a->scope.samples,0,frames*sizeof(float));
#if BB_WITH_EFFECTS
    memset(a->echo_send,0,frames*sizeof(float));memset(a->spring_send,0,frames*sizeof(float));
#endif
    a->offset=0;
#if BB_WITH_PARTS
    a->parts_error=0;
#if BB_WITH_EFFECTS
    a->wet_offset=0;
#endif
#endif
#if BB_OWNER_AUDIO_BRIDGE
    if(!bb_owner_audio_block_begin(a))BB_PROFILE_RETURN(0);
#endif
    bb_apply_pending_live(&a->core,&a->commands,audio_live_command,a);
#if BB_WITH_RICH_DRUM_RENDER
    if(a->rich.render_errors!=render_errors_before)BB_PROFILE_RETURN(0);
#endif
    if(!one_drum_audio_ok(a))BB_PROFILE_RETURN(0);
#if BB_WITH_PARTS && BB_WITH_PREPARED_BANK
    /* Stopped transport has no sequencer boundary callback. Once main has
     * prepared a requested section, the audio owner publishes it here. */
    if(!a->core.running && a->parts.parts.ready) {
        if(!pb_bind_families(&a->parts,bb_audio_families(a)))a->parts_result=BB_PART_REJECTED;
        else {
            int result=pb_part_commit(&a->parts,&a->core);
            if(result==BB_PART_APPLIED || result==BB_PART_REJECTED)a->parts_result=(uint8_t)result;
            if(result==BB_PART_APPLIED)audio_part_changed(a);
        }
    }
#endif
#if BB_WITH_EVOLVE
    bb_audio_evolve_stopped(a);
#endif
#if BB_WITH_EFFECTS
    if(!audio_effect_settings(a))BB_PROFILE_RETURN(0);
#endif
    {
        unsigned k;
        for(k=0;k<BB_MACHINES;k++)audio_sync_drum(a,k);
    }
#if BB_WITH_DILLO
    if(memcmp(&a->dillo.sound,&a->core.dillo,sizeof a->core.dillo))bb_dillo_sound(&a->dillo,&a->core.dillo);
    if(a->core.synths[0].muted)audio_release_synth(a,0);
    else if(!a->core.running && !a->keys[0].count)bb_dillo_stop(&a->dillo);
#endif
#if BB_WITH_TELEGRAM
    if(memcmp(&a->telegram.sound,&a->core.telegram,sizeof a->core.telegram))bb_telegram_sound(&a->telegram,&a->core.telegram);
    if(a->core.synths[1].muted)audio_release_synth(a,1);
    else if(!a->core.running && !a->keys[1].count)bb_telegram_stop(&a->telegram);
#endif
#if BB_P1B_CALL_JOB
    bb_p1b.call_frames=frames;bb_p1b.rejected=0;
#endif
#if BB_WITH_EVOLVE || BB_WITH_PARTS || BB_WITH_PATTERN
#if BB_WITH_DILLO || BB_WITH_TELEGRAM
    bb_process_render_hook(&a->core,frames,
#if BB_WITH_RICH_DRUM_RENDER
        audio_sequence_hit,
#else
        audio_hit,
#endif
        audio_note,audio_span,audio_before_tick,a);
#else
    bb_process_render_hook(&a->core,frames,audio_hit,0,audio_span,audio_before_tick,a);
#endif
#elif BB_WITH_DILLO || BB_WITH_TELEGRAM
    bb_process_render(&a->core,frames,audio_hit,audio_note,audio_span,a);
#else
    bb_process_render(&a->core,frames,audio_hit,0,audio_span,a);
#endif
#if BB_P1B_CALL_JOB
    bb_p1b.call_frames=0;
    if(bb_p1b.active) { /* unreachable: the last span joins or latches */
        bb_p1b.active=0;bb_p1b_guard_trips++;bb_rich_worker_cleanup();BB_PROFILE_RETURN(0);
    }
#endif
#if BB_OWNER_AUDIO_BRIDGE
    if(!bb_owner_audio_block_end(a))BB_PROFILE_RETURN(0);
#endif
#if BB_WITH_RICH_DRUM_RENDER
    /* Preserve cumulative evidence; only this block's new errors reject it. */
    if(a->rich.render_errors!=render_errors_before)BB_PROFILE_RETURN(0);
#endif
    bb_scope_capture(&a->scope,frames);
#if BB_WITH_EFFECTS
    if(a->offset!=frames)BB_PROFILE_RETURN(0);
#if BB_WITH_PARTS
    if(a->parts_error || !audio_wet(a,a->wet_offset,frames-a->wet_offset))BB_PROFILE_RETURN(0);
#else
    if(!audio_wet(a,0,frames))BB_PROFILE_RETURN(0);
#endif
#endif
#if BB_OUTPUT_GUARD
    if(a->offset!=frames)BB_PROFILE_RETURN(0);
    BB_PROFILE_BEGIN(BB_PROF_OUTPUT);
    int output_ok=bb_output_process(&a->output,a->left,a->right,frames);
    BB_PROFILE_END(BB_PROF_OUTPUT,frames);
    BB_PROFILE_RETURN(output_ok);
#else
    BB_PROFILE_RETURN(a->offset==frames);
#endif
}
void bb_audio_p1b_report(bb_p1b_report *r) {
    if(!r)return;
    memset(r,0,sizeof *r);
#if BB_P1B_CALL_JOB
    r->compiled=1;r->enable=bb_p1b_enable;r->call_jobs=bb_p1b_call_jobs;r->joins_saved=bb_p1b_joins_saved;
    r->guard_trips=bb_p1b_guard_trips;r->fallbacks=bb_p1b_fallbacks;r->guard_first_mask=bb_p1b_guard_first_mask;
    r->guard_discards=bb_p1b_guard_discards;r->guard_cpu1_faults=bb_p1b_guard_cpu1_faults;
#endif
}
static void demo_hit(bb_core *c,unsigned m,unsigned s,unsigned v,int accent) {
    c->drums[m].steps[s].hits|=(uint16_t)(1u<<v);
    if(accent)c->drums[m].steps[s].accents|=(uint16_t)(1u<<v);
}
void bb_audio_demo(bb_core *c) {
    unsigned i;
    if(!c)return;
    memset(c->drums[0].steps,0,sizeof c->drums[0].steps);
    memset(c->drums[1].steps,0,sizeof c->drums[1].steps);
    bb_set_length(c,0,16);bb_set_length(c,1,12);
    for(i=0;i<16;i+=4)demo_hit(c,0,i,D8_BD,1);
    demo_hit(c,0,4,D8_SD,0);demo_hit(c,0,12,D8_SD,1);
    for(i=0;i<16;i+=2)demo_hit(c,0,i,D8_CH,0);
    demo_hit(c,1,2,D8_CB,0);demo_hit(c,1,7,D8_RS,0);
    demo_hit(c,1,10,D8_MT,1);demo_hit(c,1,11,D8_HT,0);
}
