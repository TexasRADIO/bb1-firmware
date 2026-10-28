/* SPDX-License-Identifier: GPL-3.0-only
 * Included after storage.c/project.c and before Big Beat UI. Main loop only.
 * Uses existing project slots ONLY for blank or recognized Big Beat records.
 * No automatic boot writes and no overwriting foreign/unsupported projects.
 */
#if BB_WITH_SESSION
#include "bb/bb_family_scene.h"
#include "bb/group_scene.h"
#include "bb/bb_expression_scene.h"
#if BB_WITH_RICH_DRUM_RENDER
#include "bb/bb_rich_scene.h"
#define BB_TARGET_RICH_SCENE_BYTES BB_RICH_SCENE_OVERHEAD
#else
#define BB_TARGET_RICH_SCENE_BYTES 0u
#endif
#if BB_SAMPLE_HOST_INTEGRATION
#include "bb/sample_scene.h"
#if defined(BB_SAMPLE_DUAL_TRIAL)
#define BB_TARGET_BASE_SCENE_BYTES BB_SAMPLE_PAIR_SCENE_BYTES
static int bb_pair_session_capture(void);
static size_t bb_pair_session_encode(void);
static int bb_pair_session_decode(const void *,size_t);
static int bb_pair_session_restore(void);
#else
#define BB_TARGET_BASE_SCENE_BYTES BB_SAMPLE_SCENE_BYTES
static int bb_sample_session_capture(bb_sample_scene_identity *);
static int bb_sample_session_decode(const void *,size_t,bb_family_scene_workspace *,uint8_t *,bb_sample_scene_identity *);
static int bb_sample_session_restore(const bb_family_scene *,const uint8_t *,const bb_sample_scene_identity *,bb_sample_audio_binding *);
#endif
#else
#define BB_TARGET_BASE_SCENE_BYTES GG_SCENE_BYTES
#endif
#define BB_TARGET_SCENE_BYTES (BB_TARGET_BASE_SCENE_BYTES+BB_EXPRESSION_SCENE_OVERHEAD+BB_TARGET_RICH_SCENE_BYTES)
#if !FELUCCA_FLASH
#error "Big Beat sessions require the verified flash backend"
#endif
_Static_assert(BB_TARGET_SCENE_BYTES<=ST_PAYLOAD_MAX,"family scene fits project journal");
static struct {
#if defined(BB_SAMPLE_DUAL_TRIAL)
    bb_pair_stage pair;
    uint8_t wire[BB_TARGET_SCENE_BYTES];
    bb_sample_audio_binding sample_entries[2][8];
#else
    bb_family_scene_workspace work;
    uint8_t wire[BB_TARGET_SCENE_BYTES];
    uint8_t groups[GG_SCENE_MIX_BYTES];
    bb_note_expression expression[BB_SYNTHS][BB_STEPS];
#if BB_WITH_RICH_DRUM_RENDER
    bb_phrase rich_phrases[BB_RICH_SCENE_PARTS];
    uint8_t rich_mask;
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    bb_sample_scene_identity sample_identity;
    bb_sample_audio_binding sample_entries[8];
    uint8_t sampled;
#endif
#endif
} bb_session_memory __attribute__((section(".pool")));
static unsigned bb_session_job,bb_session_slot;
static uint32_t bb_session_started;
static const char *bb_session_message;
static uint8_t bb_session_loaded;

static int bb_session_usb_recording(void) {
#if FELUCCA_UAC
    return uac.alt!=0;
#else
    return 0;
#endif
}
static int bb_kit_switch_busy(void);
static int bb_session_busy(void){return bb_session_job!=0||bb_kit_switch_busy();}
static int bb_session_families_match(void){
    int owner=bb909_audio_layer();
    for(unsigned i=0;i<BB_MACHINES;i++)
        if(bb_sound.drum_patterns.family[i]!=(unsigned)((int)i==owner))return 0;
    return !bb_c1_909_pending.count;
}
static int bb_session_patterns_idle(void){
#if BB_WITH_RICH_DRUM_RENDER
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(bb_sound.rich.pending[m].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(bb_sound.rich.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
#endif
    for(unsigned i=0;i<BB_MACHINES;i++)if(bb_sound.drum_patterns.pending[i].ready)return 0;
    for(unsigned i=0;i<BB_SYNTHS;i++)if(bb_sound.patterns.pending[i].ready)return 0;
    for(unsigned i=0;i<BB_PATTERN_SLOTS;i++)if(bb_sound.patterns.inbox[i].state!=BB_PATTERN_SLOT_FREE||
        bb_sound.drum_patterns.inbox[i].state!=BB_PATTERN_SLOT_FREE)return 0;
    return 1;
}
#if BB_WITH_RICH_DRUM_RENDER && !defined(BB_SAMPLE_DUAL_TRIAL)
static int bb_session_rich_admit(void){
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(bb_session_memory.rich_mask&(1u<<m)){
        if(bb_session_memory.work.decoded.family[m])return 0;
        uint32_t playable=BB_RICH_808_VOICES|bb_audio_aux_capabilities(&bb_sound);
#if BB_SAMPLE_HOST_INTEGRATION
        if(!m&&bb_session_memory.sampled&&bb_session_memory.sample_identity.kind!=1)
            playable=bb_audio_sample_source_voices(&bb_sound,bb_session_memory.sample_identity.kit)&BB_RICH_REPRESENTABLE_VOICES;
#endif
        bb_phrase_index prepared;
        if(!bb_phrase_index_prepare(&bb_session_memory.rich_phrases[m],
             playable,&prepared)||
           !bb_audio_phrase_preflight(&prepared))return 0;
    }
    return 1;
}
static int bb_session_rich_capture(void){
    memset(bb_session_memory.rich_phrases,0,sizeof bb_session_memory.rich_phrases);
    bb_session_memory.rich_mask=0;
    for(unsigned m=0;m<BB_RICH_DRUMS;m++)if(bb_sound.rich.valid[m]){
        if(!bb_phrase_index_valid(&bb_sound.rich.active[m],bb_audio_phrase_source_voices_owned(&bb_sound,m))||
           !bb_audio_phrase_preflight(&bb_sound.rich.active[m]))return 0;
        bb_session_memory.rich_phrases[m]=bb_sound.rich.active[m].phrase;
        bb_session_memory.rich_mask|=(uint8_t)(1u<<m);
    }
    return 1;
}
#endif
/* Saving reads the last CPU0-owned scene, never a failed worker payload.
 * A physically held failed worker may permit a save without being retired
 * for audio. Loads still require the existing operable/recovery contract. */
static int bb_session_worker_ready(int load) {
    if(bb_target_cpu0_serial_ok())return 1; /* N1: unarmed-but-held CPU1 is serial CPU0 */
    return !load && bb_c1_live.failed && bb_c1_live.held &&
        !bb_c1_live.busy && (BB_C1_CON&10u)==2u;
}
static int bb_session_request(unsigned slot,int load) {
    uintptr_t token;
    if(bb_session_busy()){bb_session_message="BUSY / PLEASE WAIT";return 0;}
    if(bb_kit_hold_request||bb_asset_hold_request||bb_session_hold_request||
       !bb_session_worker_ready(load)){bb_session_message="WORKER NOT READY";return 0;}
    if(slot>=4 || !flash_ok){bb_session_message="STORAGE UNAVAILABLE";return 0;}
    if(bb_session_usb_recording()){bb_session_message="STOP USB RECORDING FIRST";return 0;}
    token=bb_target_enter(0);
    if(bb_sound.core.running || !bb_commands_idle(&bb_sound.commands)||
#if BB_WITH_PREPARED_BANK
       !pb_ownership_idle(&bb_sound.parts)||
#endif
       !bb_session_patterns_idle()||!bb_session_families_match()) {
        bb_target_leave(0,token);bb_session_message="STOP / WAIT THEN RETRY";return 0;
    }
    bb_session_loaded=0;bb_session_slot=slot;bb_session_job=load?2u:1u;
    bb_session_started=fm1_ms;bb_session_hold_ack=0;
    bb_kit_hold_ack=0;
    __asm__ volatile("" ::: "memory");bb_session_hold_request=bb_kit_hold_request=1;
    bb_target_leave(0,token);
    bb_session_message="PREPARING / AUDIO PAUSED";return 1;
}
/* Return 1 for an existing supported scene, 0 for blank, -1 for protected
 * foreign/corrupt content. Even a valid journal CRC needs scene validation. */
static int bb_target_scene_decode(const void *data,size_t bytes){
#if defined(BB_SAMPLE_DUAL_TRIAL)
    return bb_pair_session_decode(data,bytes);
#else
    const void *nested;size_t nested_bytes;
#if BB_WITH_RICH_DRUM_RENDER
    if(!bb_rich_scene_open(data,bytes,bb_session_memory.rich_phrases,&bb_session_memory.rich_mask,
                           &nested,&nested_bytes))return 0;
    data=nested;bytes=nested_bytes;
#endif
    /* Disposable staging only. Neither metadata nor musical state reaches
     * the renderer until the nested scene/library decoder and restore pass. */
    if(!bb_expression_scene_open(data,bytes,bb_session_memory.expression,
                                 &nested,&nested_bytes))return 0;
    data=nested;bytes=nested_bytes;
#if BB_SAMPLE_HOST_INTEGRATION
    bb_session_memory.sampled=0;
    if(bytes>=4&&!memcmp(data,"BBKS",4)){
        if(!bb_sample_session_decode(data,bytes,&bb_session_memory.work,bb_session_memory.groups,
                                    &bb_session_memory.sample_identity))return 0;
        bb_session_memory.sampled=1;
#if BB_WITH_RICH_DRUM_RENDER
        return bb_session_rich_admit();
#else
        return 1;
#endif
    }
#endif
    if(!gg_scene_decode_staged(data,bytes,44100,&bb_session_memory.work,bb_session_memory.groups))return 0;
#if BB_WITH_RICH_DRUM_RENDER
    return bb_session_rich_admit();
#else
    return 1;
#endif
#endif
}
static int bb_session_slot_state(unsigned obj) {
    st_hdr_t h;int current=st_current(obj,&h);
    if(current>=0) {
        if(!bb_target_scene_decode(st_buf,h.len))return -1;
        return 1;
    }
    /* No committed copy: only erased headers count as blank. A torn first
     * save with an erased commit header is retryable; opaque header damage
     * is protected, not automatically reformatted. */
    for(unsigned copy=0;copy<2;copy++) {
        uint8_t header[32];
        if(st_read(st_sector(obj,copy),header,sizeof header))return -1;
        for(unsigned i=0;i<sizeof header;i++)if(header[i]!=255)return -1;
    }
    return 0;
}
static void bb_session_release(void) {
    __asm__ volatile("" ::: "memory");bb_session_hold_request=bb_kit_hold_request=0;
    bb_session_job=0;
}
/* Returns 1 exactly when a job completes; UI then consumes message/loaded. */
static __attribute__((noinline)) int bb_session_poll(void) {
    unsigned obj;int state;
    if(!bb_session_job)return 0;
    if(bb_session_job==3)return 0; /* latched reset failure: keep silent */
    if(!bb_session_hold_ack||!bb_kit_hold_ack) {
        if((uint32_t)(fm1_ms-bb_session_started)<1000u)return 0;
        bb_session_message="AUDIO PAUSE TIMEOUT";bb_session_release();return 1;
    }
    __asm__ volatile("" ::: "memory");
    if(bb_session_usb_recording()) {
        bb_session_message="USB RECORDING / CANCELLED";bb_session_release();return 1;
    }
    if(!bb_session_families_match()||!bb_session_patterns_idle()) {
        bb_session_message="KIT CHANGED / CANCELLED";bb_session_release();return 1;
    }
    /* Recheck after the pause handshake, before any flash access. */
    if(!bb_session_worker_ready(bb_session_job==2)) {
        bb_session_message="WORKER NOT READY / CANCELLED";bb_session_release();return 1;
    }
    obj=OBJ_PROJECT0+bb_session_slot;
    state=bb_session_slot_state(obj);
    if(state<0){bb_session_message="PROTECTED / NOT BIG BEAT";bb_session_release();return 1;}
    /* No restore has begun. A failed precondition must release the pause;
     * only a failure after live-state mutation may retain the reset hold. */
    if(!pab_target_session_ready(&bb_sound)) {
        bb_session_message="OWNER NOT READY / CANCELLED";bb_session_release();return 1;
    }
    if(bb_session_job==1) {
        if(!pab_session_snapshot_groups(&bb_sound,1)) {
            bb_session_message="OWNER STATE ERROR / NOT SAVED";bb_session_release();return 1;
        }
#if defined(BB_SAMPLE_DUAL_TRIAL)
        size_t bytes=bb_pair_session_capture()?bb_pair_session_encode():0;
#else
        bb_family_scene *s=&bb_session_memory.work.decoded;
        memset(s,0,sizeof *s);s->core=bb_sound.core;
        memcpy(bb_session_memory.expression,bb_sound.patterns.expression,
               sizeof bb_session_memory.expression);
        memcpy(s->family,bb_sound.drum_patterns.family,4);
        int owner=bb909_audio_layer();if(owner>=0)s->patch[owner]=bb_c1_909_controls;
        size_t bytes=0;
        if(gg_scene_capture(bb_sound.groups,bb_session_memory.groups)
#if BB_WITH_RICH_DRUM_RENDER
           &&bb_session_rich_capture()
#endif
           ){
#if BB_SAMPLE_HOST_INTEGRATION
            if(bb_sound.sample){
                if(bb_sample_session_capture(&bb_session_memory.sample_identity))
                    bytes=bb_sample_scene_encode(s,bb_session_memory.groups,&bb_session_memory.sample_identity,
                                                bb_session_memory.wire+BB_TARGET_RICH_SCENE_BYTES+BB_EXPRESSION_SCENE_OVERHEAD,
                                                BB_TARGET_BASE_SCENE_BYTES);
            }else
#endif
            bytes=gg_scene_encode(s,bb_session_memory.groups,
                                  bb_session_memory.wire+BB_TARGET_RICH_SCENE_BYTES+BB_EXPRESSION_SCENE_OVERHEAD,
                                  BB_TARGET_BASE_SCENE_BYTES);
        }
        if(bytes)bytes=bb_expression_scene_finish(bb_session_memory.wire+BB_TARGET_RICH_SCENE_BYTES,
                         sizeof bb_session_memory.wire-BB_TARGET_RICH_SCENE_BYTES,bytes,bb_session_memory.expression);
#if BB_WITH_RICH_DRUM_RENDER
        if(bytes)bytes=bb_rich_scene_finish(bb_session_memory.wire,sizeof bb_session_memory.wire,
                         bytes,bb_session_memory.rich_phrases,bb_session_memory.rich_mask);
#endif
#endif
        if(!bytes)
            bb_session_message="INVALID SCENE / NOT SAVED";
        else if(st_save(obj,bb_session_memory.wire,(uint32_t)bytes))
            bb_session_message="SAVE FAILED / KEEP SCENE";
        else bb_session_message="SAVED / FLASH VERIFIED";
    } else if(!state)bb_session_message="EMPTY SCENE";
    else {
        int n=st_load(obj,bb_session_memory.wire,sizeof bb_session_memory.wire);
        int result=BB_C1_SCENE_REJECTED;
        if(n>0&&bb_target_scene_decode(bb_session_memory.wire,(size_t)n)){
#if defined(BB_SAMPLE_DUAL_TRIAL)
            result=bb_pair_session_restore();
#else
#if BB_SAMPLE_HOST_INTEGRATION
            if(bb_session_memory.sampled)
                result=bb_sample_session_restore(&bb_session_memory.work.decoded,bb_session_memory.groups,
                                                &bb_session_memory.sample_identity,bb_session_memory.sample_entries);
            else
#endif
            result=bb_c1_family_scene_restore(&bb_sound,&bb_session_memory.work.decoded,bb_session_memory.groups);
#endif
        }
#if BB_WITH_RICH_DRUM_RENDER && !defined(BB_SAMPLE_DUAL_TRIAL)
        if(result==BB_C1_SCENE_OK&&!bb_audio_phrase_scene_install_reset(&bb_sound,
              bb_session_memory.rich_phrases,bb_session_memory.rich_mask))result=BB_C1_SCENE_HELD_FAILURE;
#endif
        if(result==BB_C1_SCENE_HELD_FAILURE) {
            /* Do not let a half-initialized engine run. UBOOT/USB recovery
             * remain available. Keep this hold until restart. */
            bb_session_message="AUDIO RESET ERROR / RESTART";bb_session_job=3;return 1;
        }
        if(result!=BB_C1_SCENE_OK)bb_session_message="LOAD FAILED / KEEP SCENE";
        else {
            /* Both owners remain suspended here. Legacy scenes supplied zero
             * sidecars; failed decodes/restores never publish staged metadata. */
#if !defined(BB_SAMPLE_DUAL_TRIAL)
            memcpy(bb_sound.patterns.expression,bb_session_memory.expression,
                   sizeof bb_sound.patterns.expression);
#endif
            bb_session_loaded=1;bb_session_message="LOADED / PRESS PLAY";
            bb_ticks=0;bb_visual_peak=0;
            for(unsigned k=0;k<BB_MACHINES;k++)bb_playhead[k]=0;
            for(unsigned k=0;k<BB_SYNTHS;k++)bb_synth_playhead[k]=0;
        }
    }
    bb_session_release();return 1;
}
#endif
