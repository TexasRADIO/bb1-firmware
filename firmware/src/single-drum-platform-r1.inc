/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0-only scene integration. The TARGET must obtain these observations
 * from its real pause/producer guards. Passing this structure is NOT proof
 * of IRQ exclusion. No flash, worker, transport or recovery operations here. */
#include "bb_family_scene.h"
#include "group_scene.h"
#include "bb_one_drum_profile.h"
typedef struct {
    unsigned requested,acknowledged,producers_frozen,audio_entered,usb_recording;
} bb_single_pause;
enum {BB_SINGLE_REJECTED,BB_SINGLE_OK,BB_SINGLE_HELD_FAILURE};
static int bb_single_paused(const bb_audio *a,const bb_single_pause *p) {
    if(!a||!p||p->requested!=1||p->acknowledged!=1||p->producers_frozen!=1||
       p->audio_entered||p->usb_recording||!a->groups||!bb_one_drum_core_ok(&a->core)||
       a->core.sample_rate!=44100||a->core.running||!bb_commands_idle((bb_commands *)&a->commands)||
       (!a->commands.enter!=!a->commands.leave)||!bb_audio_groups_outside(a,p,sizeof *p))return 0;
    for(unsigned i=0;i<BB_MACHINES;i++)if(a->drum_patterns.pending[i].ready)return 0;
    for(unsigned i=0;i<BB_SYNTHS;i++)if(a->patterns.pending[i].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->patterns.inbox[i].state!=BB_PATTERN_SLOT_FREE||
       a->drum_patterns.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#if BB_WITH_RICH_DRUM_RENDER
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(a->rich.pending[m].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(a->rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#endif
    return 1;
}
static int bb_single_capture(bb_audio *a,bb_family_scene *s,uint8_t *mix,const bb_single_pause *p) {
#if defined(BB_SAMPLE_DUAL_TRIAL)
    if(a&&a->sample_b)return BB_SINGLE_REJECTED; /* legacy capture omits B identity */
#endif
    if(!bb_single_paused(a,p)||!s||!mix||!bb_audio_groups_outside(a,s,sizeof *s)||
       !bb_audio_groups_outside(a,mix,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(s,sizeof *s,mix,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(s,sizeof *s,p,sizeof *p)||!bb_wire_separated(mix,GG_SCENE_MIX_BYTES,p,sizeof *p)||
       !bb_session_state_valid(&a->core))return BB_SINGLE_REJECTED;
    /* gg_scene_capture validates all input levels before first output write.
     * No private-worker copy can overwrite the CPU0 mix. */
    if(!gg_scene_capture(a->groups,mix))return BB_SINGLE_REJECTED;
    memset(s,0,sizeof *s);s->core=a->core;return BB_SINGLE_OK;
}
static int bb_single_restore_base(bb_audio *a,const bb_family_scene *s,const uint8_t *mix,const bb_single_pause *p) {
    if(!bb_single_paused(a,p)||!s||!mix||!bb_audio_groups_outside(a,s,sizeof *s)||
       !bb_audio_groups_outside(a,mix,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(s,sizeof *s,mix,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(s,sizeof *s,p,sizeof *p)||!bb_wire_separated(mix,GG_SCENE_MIX_BYTES,p,sizeof *p)||
       !bb_one_drum_core_ok(&s->core)||bb_family_scene_admit(s,0)!=BB_FAMILY_ADMISSION_OK||
       s->core.running||s->core.sample_rate!=44100)return BB_SINGLE_REJECTED;
    /* Geometry above proves gg_scene_restore cannot overlap; every encoded
     * mix byte is a valid level/mute combination. Preserve the real group
     * allocation, never a private snapshot or pointer into the old actor. */
    gg_state *groups=a->groups;bb_enter_fn enter=a->commands.enter;bb_leave_fn leave=a->commands.leave;
    void *context=a->commands.lock_context;
    if(!bb_audio_init(a,enter,leave,context))return BB_SINGLE_HELD_FAILURE;
    a->groups=groups;
    if(!gg_scene_restore(a->groups,mix))return BB_SINGLE_HELD_FAILURE;
    a->core=s->core;
    for(unsigned k=0;k<BB_MACHINES;k++){
        a->drum_revision[k]=0;
        a->mix_gain[k]=((uint32_t)a->core.drums[k].level*65536u+63u)/127u;
    }
    for(unsigned k=0;k<BB_MIX_CHANNELS;k++)
        a->band_gain[k]=((uint32_t)a->core.band_level[k]*65536u+63u)/127u;
    return BB_SINGLE_OK;
}
static int bb_single_restore(bb_audio *a,const bb_family_scene *s,const uint8_t *mix,const bb_single_pause *p){
#if BB_SAMPLE_HOST_INTEGRATION
    /* A legacy scene has no sample identity. Never silently turn a sampled
     * machine into an 808 by wiping its binding during the audio reset. */
    if(a&&a->sample)return BB_SINGLE_REJECTED;
#endif
    return bb_single_restore_base(a,s,mix,p);
}
#if BB_SAMPLE_HOST_INTEGRATION
static int bb_single_restore_sample_impl(bb_audio *a,const bb_family_scene *s,const uint8_t *mix,
                                    const bb_single_pause *p,bb_sample_audio_binding *current,
                                    const bb_sample_audio_binding *entries,unsigned count,unsigned selected
#if BB_WITH_RICH_DRUM_RENDER
                                    ,const bb_phrase *replacement
#endif
                                    ){
#if defined(BB_SAMPLE_DUAL_TRIAL)
    if(a&&a->sample_b)return BB_SINGLE_REJECTED; /* use paired restore, never discard B */
#endif
    if(!bb_single_paused(a,p)||!s||!mix||!current||!entries)return BB_SINGLE_REJECTED;
#if BB_WITH_RICH_DRUM_RENDER
    if(replacement){
        if(!bb_audio_groups_outside(a,replacement,sizeof *replacement)||
           !bb_sample_separate(replacement,sizeof *replacement,current,sizeof *current)||
           !bb_sample_separate(replacement,sizeof *replacement,current->slots,current->count*sizeof *current->slots)||
           !bb_audio_sample_restore_check_phrase(a,&s->core,current,entries,count,selected,replacement))return BB_SINGLE_REJECTED;
    }else
#endif
    if(!bb_audio_sample_restore_check(a,&s->core,current,entries,count,selected))return BB_SINGLE_REJECTED;
    /* The scene and transaction descriptors must survive the reset and voice
     * clearing. Validate output overlap before touching any live state. */
    const void *inputs[]={s,mix,p,entries};
    const size_t sizes[]={sizeof *s,GG_SCENE_MIX_BYTES,sizeof *p,count*sizeof *entries};
    for(unsigned i=0;i<4;i++)if(!bb_audio_groups_outside(a,inputs[i],sizes[i])||
        !bb_sample_separate(inputs[i],sizes[i],current,sizeof *current)||
        !bb_sample_separate(inputs[i],sizes[i],current->slots,current->count*sizeof *current->slots))return BB_SINGLE_REJECTED;
    int result=bb_single_restore_base(a,s,mix,p);
    if(result!=BB_SINGLE_OK)return result;
    /* Commit under the same pause. Clear old voices before reusing their
     * slots, then atomically publish the saved kit and its admission hook. */
    memset(current->slots,0,current->count*sizeof *current->slots);
    unsigned descriptor=selected;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
    if(selected==BB_SAMPLE_808_ID)descriptor=0;
#endif
    *current=entries[descriptor];
#if BB_WITH_RICH_DRUM_RENDER
    if(replacement){
        if(!bb_audio_sample_startup_phrase(a,current,entries,count,selected,replacement))return BB_SINGLE_HELD_FAILURE;
    }else
#endif
    if(!bb_audio_sample_startup(a,current,entries,count,selected))return BB_SINGLE_HELD_FAILURE;
    return BB_SINGLE_OK;
}
static int bb_single_restore_sample(bb_audio *a,const bb_family_scene *s,const uint8_t *mix,
                                    const bb_single_pause *p,bb_sample_audio_binding *current,
                                    const bb_sample_audio_binding *entries,unsigned count,unsigned selected){
    return bb_single_restore_sample_impl(a,s,mix,p,current,entries,count,selected
#if BB_WITH_RICH_DRUM_RENDER
                                        ,NULL
#endif
                                        );
}
#endif
