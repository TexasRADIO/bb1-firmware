/* SPDX-License-Identifier: GPL-3.0-only */
/* Exclusive cold UI measurement, BEFORE bb_ui_init/audio_init/usb_start.
 * The instrumented callbacks do NOT mask/unmask IRQs. Their timings describe
 * guarded body work, NOT live interrupt-blocking duration or entry latency. */
#if BB_RICH_BOOT_QUALIFY && BB_WITH_EVOLVE && BB_SNAPSHOT_STARTUP_DIAGNOSTIC
#include "bb/bb_crc32.h"
typedef struct {uint32_t tracking,phase,start,depth;} bb_snapshot_guard_state;
static bb_snapshot_guard_state bb_snapshot_guard;
static uintptr_t bb_snapshot_enter(void *ctx){
    (void)ctx;
    if(bb_snapshot_guard.tracking){
        if(bb_snapshot_guard.depth++)bb_snapshot_probe.failures++;
        bb_snapshot_probe.guards[bb_snapshot_guard.phase]++;
        bb_snapshot_guard.start=fm1_ticks();
    }
    return 1;
}
static void bb_snapshot_leave(void *ctx,uintptr_t token){
    (void)ctx;
    if(!bb_snapshot_guard.tracking)return;
    if(token!=1||bb_snapshot_guard.depth!=1){bb_snapshot_probe.failures++;bb_snapshot_guard.depth=0;return;}
    uint32_t elapsed=fm1_ticks()-bb_snapshot_guard.start;
    uint32_t *max=&bb_snapshot_probe.guard_max[bb_snapshot_guard.phase];
    if(elapsed>*max)*max=elapsed;
    bb_snapshot_guard.depth=0;
}
/* Exact retained r422 reference body, renamed ONLY for this comparison. */
static void bb_snapshot_reference_sync(void) {
    bb_commands *q=&bb_sound.commands;bb_evo_snapshot view;
    memset(&view,0,sizeof view);
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    if(!bb_commands_idle(q)){if(q->leave)q->leave(q->lock_context,token);return;}
    view.settings=bb_sound.evolve.settings;view.changes=bb_sound.evolve.changes;
    view.ready=bb_sound.evolve.ready;view.enabled=bb_sound.evolve.enabled;
    view.frozen=bb_sound.evolve.frozen;view.conflict=bb_sound.evolve.conflict;
    view.pending=bb_sound.evolve_home_pending;view.result=bb_sound.evolve_result;
    if(q->leave)q->leave(q->lock_context,token);
    if(memcmp(&view,&bb_evo_view,sizeof view)){bb_evo_view=view;bb_dirty=1;}
    if(view.ready && bb_panel_sync_patterns(&bb_face,q,&bb_sound.core)==2)bb_dirty=1;
}
static int bb_snapshot_zero(const void *p,unsigned n){
    const uint8_t *b=p;for(unsigned i=0;i<n;i++)if(b[i])return 0;return 1;
}
static int bb_snapshot_patterns_equal(void){
    for(unsigned m=0;m<BB_MACHINES;m++){
        const bb_machine *a=&bb_face.requested.drums[m],*b=&bb_sound.core.drums[m];
        if(a->length!=b->length||a->sound_revision!=b->sound_revision||
           memcmp(a->steps,b->steps,sizeof a->steps)||memcmp(a->sound.mode,b->sound.mode,sizeof a->sound.mode))return 0;
    }
    for(unsigned s=0;s<BB_SYNTHS;s++){
        const bb_sequence *a=&bb_face.requested.synths[s],*b=&bb_sound.core.synths[s];
        if(a->length!=b->length||memcmp(a->notes,b->notes,sizeof a->notes))return 0;
    }
    return 1;
}
static void __attribute__((noinline)) bb_snapshot_diagnostic_startup(void){
    memset(&bb_snapshot_probe,0,sizeof bb_snapshot_probe);
    bb_snapshot_probe.version=1;bb_snapshot_probe.polls=BB_SNAPSHOT_POLLS;
    bb_snapshot_probe.status=BB_SNAPSHOT_ADMISSION;
    /* Resetting to zero is permitted ONLY after proving those exact initial
     * bytes. Never call this diagnostic on a live or already initialized UI. */
    if(!bb_single_startup_admitted()||!bb_commands_idle(&bb_sound.commands)||
       !bb_sound.commands.enter||!bb_sound.commands.leave||
       !bb_snapshot_zero(&bb_face,sizeof bb_face)||!bb_snapshot_zero(&bb_evo_view,sizeof bb_evo_view)||
       !bb_snapshot_zero(&bb_sound.evolve,sizeof bb_sound.evolve)||bb_evo_sync_pending)return;
    bb_enter_fn enter=bb_sound.commands.enter;bb_leave_fn leave=bb_sound.commands.leave;
    void *context=bb_sound.commands.lock_context;
    uint8_t dirty=bb_dirty;
    uint32_t core_crc=bb_crc32_add(0,(const uint8_t *)&bb_sound.core,sizeof bb_sound.core);
    uint32_t icfg=fm1_icfg(),faults=bb_audio_faults;
    bb_snapshot_probe.status=BB_SNAPSHOT_STATE;
    memset(&bb_snapshot_guard,0,sizeof bb_snapshot_guard);
    bb_sound.commands.enter=bb_snapshot_enter;bb_sound.commands.leave=bb_snapshot_leave;
    bb_sound.commands.lock_context=0;
    if(!bb_evolve_init_families(&bb_sound.evolve,&bb_sound.core,0x42425350u,bb_sound.drum_patterns.family))goto restore;
    for(unsigned phase=0;phase<BB_SNAPSHOT_PHASES;phase++){
        int reference=phase==0||phase==3;
        bb_snapshot_guard.tracking=0;
        memset(&bb_evo_view,0,sizeof bb_evo_view);bb_evo_sync_pending=0;
        bb_evo_sync(); /* Prime both paths identically; warm-up is not timed. */
        bb_snapshot_guard.phase=phase;bb_snapshot_guard.depth=0;
        fm1_wdt_feed();
        uint32_t start=fm1_ticks();bb_snapshot_guard.tracking=1;
        for(unsigned poll=0;poll<BB_SNAPSHOT_POLLS;poll++){
            if(phase==4)bb_sound.evolve.changes++; /* Changed metadata, no core mutation. */
            if(reference)bb_snapshot_reference_sync();else bb_evo_sync();
        }
        bb_snapshot_guard.tracking=0;
        uint32_t elapsed=fm1_ticks()-start;bb_snapshot_probe.ticks[phase]=elapsed;
        if(!elapsed||elapsed>=6000000u){bb_snapshot_probe.status=BB_SNAPSHOT_TIME;goto restore;}
        unsigned expected=(reference||phase==4)?2u*BB_SNAPSHOT_POLLS:BB_SNAPSHOT_POLLS;
        if(bb_snapshot_probe.guards[phase]!=expected||!bb_snapshot_probe.guard_max[phase]||
           bb_snapshot_guard.depth||bb_snapshot_probe.failures){bb_snapshot_probe.status=BB_SNAPSHOT_GUARD;goto restore;}
        if(!bb_snapshot_patterns_equal()){bb_snapshot_probe.status=BB_SNAPSHOT_STATE;goto restore;}
        bb_snapshot_probe.completed++;
    }
    bb_snapshot_probe.status=BB_SNAPSHOT_PASS;
restore:
    bb_snapshot_guard.tracking=0;
    bb_sound.commands.enter=enter;bb_sound.commands.leave=leave;bb_sound.commands.lock_context=context;
    memset(&bb_sound.evolve,0,sizeof bb_sound.evolve);
    memset(&bb_face,0,sizeof bb_face);memset(&bb_evo_view,0,sizeof bb_evo_view);
    bb_evo_sync_pending=0;bb_dirty=dirty;
    if(core_crc!=bb_crc32_add(0,(const uint8_t *)&bb_sound.core,sizeof bb_sound.core)||
       fm1_icfg()!=icfg||bb_audio_faults!=faults||!bb_commands_idle(&bb_sound.commands)||
       bb_sound.commands.enter!=enter||bb_sound.commands.leave!=leave||bb_sound.commands.lock_context!=context||
       !bb_snapshot_zero(&bb_sound.evolve,sizeof bb_sound.evolve)||
       !bb_snapshot_zero(&bb_face,sizeof bb_face)||!bb_snapshot_zero(&bb_evo_view,sizeof bb_evo_view)||bb_evo_sync_pending)
    {
        bb_snapshot_probe.status=BB_SNAPSHOT_RESTORE;
        /* An unexpected owner-state fault must not reach physical output.
         * Preserve the evidence and use the existing cold session hold. */
        bb_session_hold_request=1;
    }
    else bb_snapshot_probe.restored=1;
}
#else
static void bb_snapshot_diagnostic_startup(void){}
#endif
