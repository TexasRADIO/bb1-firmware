/* Included after seq.c, before audio.c in the prepared Felucca translation unit.
 * Big Beat owns music state; Felucca retains the HAL, USB audio and recovery.
 * Four reference 808 slots, one enabled at first entry; optional synth demos.
 * No false Telegram substitution.
 */
#include "bb/bb_audio.h"
#include "bb/bb_audio_trio.h"
#include "bb/bb_profile.h"
#include "bb/bb_rich_probe.h"
#include "bb/bb_snapshot_probe.h"
#include "bb/bb_t5_service_timing.h"
#if BB_RICH_BOOT_QUALIFY
static bb_rich_probe_report bb_rich_probe __attribute__((section(".pool")));
#if BB_WITH_EVOLVE && BB_SNAPSHOT_STARTUP_DIAGNOSTIC
static bb_snapshot_probe_report bb_snapshot_probe __attribute__((section(".pool")));
#endif
#endif
#if BB_SAMPLE_HOST_INTEGRATION
#include "bb/bb_sample_probe.h"
static bb_sample_probe_report bb_sample_probe __attribute__((section(".pool")));
#endif
#ifndef BB_AUDIO_NATIVE_HALF
#define BB_AUDIO_NATIVE_HALF 0
#endif
#if BB_AUDIO_NATIVE_HALF
/* Audio-owner-only cache. Each128-frame quantum invalidates it before render;
 * main-loop controls cannot publish between the four routing callbacks. */
static int32_t bb_half_output[256] __attribute__((section(".pool")));
static unsigned bb_half_active,bb_half_ready,bb_half_at;
static __attribute__((noinline)) void bb_target_half_begin(void) {
    bb_half_active=1;bb_half_ready=bb_half_at=0;
}
#define bb_target_audio bb_target_audio_legacy
#endif

#ifndef BB_WITH_SESSION
#define BB_WITH_SESSION 0
#endif
#if BB_WITH_SESSION
/* Same CPU, main-loop request/audio acknowledgement. Main may touch the DSP
 * only after ack, and blocks all producers until publishing release. */
static volatile uint32_t bb_session_hold_request,bb_session_hold_ack;
static volatile uint32_t bb_asset_hold_request,bb_asset_hold_ack;
volatile uint32_t bb_kit_hold_request,bb_kit_hold_ack;
static int32_t bb_session_last_output[2];
static unsigned bb_session_resume;
static int bb_session_silent_output(int32_t *out,uint32_t n) {
    if(!bb_kit_hold_request)bb_kit_hold_ack=0;
    if(!bb_session_hold_request && !bb_asset_hold_request && !bb_kit_hold_request){
        if(bb_session_hold_ack)bb_session_resume=1;
        bb_session_hold_ack=0;return 0;
    }
    if(!bb_session_hold_ack && n) {
        unsigned fade=n;
#if BB_AUDIO_NATIVE_HALF
        if(bb_half_active && fade>32u)fade=32u;
#endif
        for(uint32_t i=0;i<n;i++)for(unsigned ch=0;ch<2;ch++)
            out[i*2u+ch]=i<fade?(int32_t)((bb_session_last_output[ch]*(int32_t)(fade-1u-i))/(int32_t)fade):0;
        bb_session_last_output[0]=bb_session_last_output[1]=0;
    } else memset(out,0,n*2u*sizeof *out);
    __asm__ volatile("" ::: "memory");
    bb_session_hold_ack=1;
    if(bb_asset_hold_request)bb_asset_hold_ack=1;
    if(bb_kit_hold_request)bb_kit_hold_ack=1;
    return 1;
}
#endif
#if BB_AUDIO_PROFILE
uint32_t bb_profile_clock(void) {return fm1_ticks();}
#endif
#if BB_QUIET_STAGE_PROBE
uint32_t bb_stage_probe_clock(void) {return fm1_ticks();}
#endif
#define BB_HARDWARE_STARTER_16 1
static bb_audio bb_sound __attribute__((section(".pool")));
static volatile uint8_t bb_audio_entered;
static volatile uint8_t bb_playhead[BB_MACHINES];
static volatile uint8_t bb_synth_playhead[BB_SYNTHS];
static volatile uint32_t bb_ticks;
static volatile uint32_t bb_audio_faults;
/* A render refusal is a failed audio consumer, not a sound-picker timeout.
 * Main-loop UI reads this scalar. A complete clean block clears it;
 * cumulative fault evidence never controls command availability. */
static volatile uint32_t bb_audio_render_failed;
/* N1 (reliability r1): CPU1 not armed is not an audio fault. With no fault
 * latched and CPU1 either armed or verified held (boot qualification failed,
 * asset-update hold), bb_rich_worker_begin() returns -1 and CPU0 renders the
 * exact serial route. "Held" is the same physical test as every other hold
 * check ((BB_C1_CON&10u)==2u); a latched fault keeps the existing contract. */
static int bb_target_cpu0_serial_ok(void) {
    if(!*(volatile uint32_t *)&bb_c1_live.failed)
        return *(volatile uint32_t *)&bb_c1_live.ready!=0 || (BB_C1_CON&10u)==2u;
    return bb_c1_cpu0_available();
}
/* D2 (reliability r1): a render error that repeats must not leave PLAY lit
 * over silence. After BB_RENDER_FAULT_STOP_MS of CONSECUTIVE failed audio the
 * audio owner requests STOP through the normal STOP lane (applied by the next
 * block's drain, which precedes every render check) and publishes a notice
 * the main loop shows once. Far longer than any transient (a one-off error
 * still recovers silently in one block), short enough that the user sees STOP
 * instead of wondering.
 * boot39 rebase, PR #17 review F1/F2: the run is counted in FRAMES, so the
 * threshold is the same in ms for 128-frame native-half quanta (16 calls, as
 * installed) and for 32-frame calls (64 calls); and a session/asset/kit hold
 * resets it (bb_target_audio), so failures either side of a hold never add. */
#ifdef BB_RENDER_FAULT_STOP_BLOCKS
#error "D2 threshold is BB_RENDER_FAULT_STOP_MS (milliseconds), not blocks"
#endif
#ifndef BB_RENDER_FAULT_STOP_MS
#define BB_RENDER_FAULT_STOP_MS 46u
#endif
/* 46 ms at 44.1 kHz = 2,029 frames: reached on the 16th 128-frame block. */
#define BB_RENDER_FAULT_STOP_FRAMES ((BB_RENDER_FAULT_STOP_MS*44100u+999u)/1000u)
static uint32_t bb_render_reject_frames;        /* audio owner only; saturates */
static volatile uint32_t bb_render_fault_stops; /* audio writes; main reads; monotonic */
/* D1 (reliability r1): set once the CPU0 fault drain has run for a latched,
 * non-retirable worker fault. Audio writes; main reads. Never cleared. */
static volatile uint32_t bb_worker_fault_drained;
static void bb_target_render_fault_note(int failed,uint32_t frames) {
    if(!failed){bb_render_reject_frames=0;return;}
    if(bb_render_reject_frames<BB_RENDER_FAULT_STOP_FRAMES)
        bb_render_reject_frames=frames>=BB_RENDER_FAULT_STOP_FRAMES-bb_render_reject_frames?
            BB_RENDER_FAULT_STOP_FRAMES:bb_render_reject_frames+frames;
    if(bb_render_reject_frames==BB_RENDER_FAULT_STOP_FRAMES && bb_sound.core.running &&
       !bb_sound.commands.stop_pending && !bb_sound.commands.stop_inflight) {
        bb_command stop={0};stop.type=BB_CMD_TRANSPORT;
        if(bb_command_push(&bb_sound.commands,&stop))bb_render_fault_stops++;
    }
}
static int bb_target_audio_operable(void) {
    return bb_target_cpu0_serial_ok() && !bb_audio_render_failed;
}
/* Decorative output envelope, not a precision peak/VU meter. Sample one
 * stereo frame in sixteen after conversion; one audio writer publishes a
 * single aligned word. UI never scans or changes the audio buffers. */
#define BB_SONIC_VISUAL 1
static volatile uint32_t bb_visual_peak;
static unsigned bb_visual_sample_phase;
static void bb_visual_measure(const int32_t *out,unsigned n) {
    uint32_t peak=0,held=bb_visual_peak;
    unsigned i=(16u-bb_visual_sample_phase)&15u;
    for(;i<n;i+=16u)for(unsigned channel=0;channel<2u;channel++) {
        int32_t value=out[i*2u+channel];
        uint32_t magnitude=value<0?0u-(uint32_t)value:(uint32_t)value;
        if(magnitude>peak)peak=magnitude;
    }
    bb_visual_sample_phase=(bb_visual_sample_phase+n)&15u;
    /* Approx. 93 ms release at 44.1 kHz. Ceil ensures eventual exact silence. */
    uint32_t fall=(held*n+4095u)/4096u;
    held=fall<held?held-fall:0u;
    bb_visual_peak=peak>held?(peak>32767u?32767u:peak):held;
}
/* This queue has exactly one main-loop producer and one ALNK0 consumer on CPU0.
 * TIMER5 never accesses it. The consumer cannot be preempted by the main loop;
 * do not blindly use the upstream main-only cli/sti pair inside an ISR.
 * Main-loop critical sections start with interrupts enabled and are bounded.
 * A future second-core producer requires a different synchronization protocol. */
static uintptr_t bb_target_enter(void *ctx) {
    (void)ctx;
    if(bb_audio_entered)return 0;
    fm1_irq_off();return 1;
}
static void bb_target_leave(void *ctx,uintptr_t token) {
    (void)ctx;if(token)fm1_irq_on();
}
/* Called only AFTER core1 boot qualification, before physical audio. */
static gg_state bb_single_target_groups[BB_MACHINES] __attribute__((section(".pool")));
static void bb_target_trio_init(void) {
    /* CPU0 owns these groups. No adoption of a private worker or program. */
    if(!bb_audio_groups_bind(&bb_sound,bb_single_target_groups,BB_MACHINES))bb_audio_faults++;
}
#define BB_PLAY_FIRST_BOOT 1
static void bb_target_init(void) {
    unsigned k;
    /* main.c calls this before core1 qualification and physical audio. */
    drum808_prepare_curves();
    bb_visual_peak=0;bb_visual_sample_phase=0;
    bb_audio_init(&bb_sound,bb_target_enter,bb_target_leave,0);
    bb_audio_demo(&bb_sound.core);
    /* The unequal-length native demo is a timing regression fixture, not the
     * instrument's welcome groove. All hardware parts start with one bar. */
    for(k=0;k<BB_MACHINES;k++)bb_set_length(&bb_sound.core,k,16);
    /* One recognisable starter kit. Additional layers keep their stored
     * phrase/sound and are explicitly added from the kit shelf. */
    for(k=1;k<BB_MACHINES;k++)bb_set_enabled(&bb_sound.core,k,0);
#if BB_WITH_DILLO
    /* Original sixteen-step Big Beat audition; each step's slide connects
     * forward into the next gated note, never into the current note. */
    {
        static const uint8_t notes[16]={36,36,43,39,36,48,43,51,36,36,43,39,36,46,48,43};
        for(k=0;k<16;k++)
            bb_sound.core.synths[0].notes[k]=(bb_note){notes[k],(uint8_t)(k!=7 && k!=15),
                (uint8_t)(k%4u==0u),(uint8_t)(k==2u || k==5u || k==10u || k==13u)};
    }
#endif
#if BB_WITH_TELEGRAM
    {
        static const uint8_t notes[16]={60,63,0,70,67,0,60,58,0,63,65,0,70,67,0,60};
        for(k=0;k<16;k++)
            bb_sound.core.synths[1].notes[k]=(bb_note){notes[k],(uint8_t)(notes[k]!=0),
                (uint8_t)(notes[k]!=0 && k%4u==0u),0};
    }
#endif
    for(k=0;k<NTRK;k++)trk[k].p[P_MUTE]=1;
    song.playing=0;
}
static void bb_target_audio(int32_t *out,uint32_t n) {
    uint32_t i;
#if BB_WITH_SESSION
    /* This check precedes EVERY bb_sound access, including fault counters.
     * While held, main can clear/rebuild that entire object with IRQs enabled. */
    if(bb_session_silent_output(out,n)){bb_visual_peak=0;bb_render_reject_frames=0;return;}
#endif
#if BB_OUTPUT_GUARD
    if(bb_c1_live.failed) {
        int retired=bb_c1_retire_rich_failed();
        if(!retired){
            /* D1 (reliability r1): output stays off until reboot, but the
             * control plane must not freeze. With CPU1 verified held (the
             * same test that already permits save), drain on CPU0 only:
             * STOP applies, START is refused, every queued request ends
             * visibly. No render, no CPU1 access, no payload reuse. */
            if(bb_c1_live.held && !bb_c1_live.busy && (BB_C1_CON&10u)==2u) {
                bb_audio_entered=1;
                (void)bb_audio_fault_drain(&bb_sound);
                song.playing=bb_sound.core.running;
                bb_audio_entered=0;bb_worker_fault_drained=1;
            }
            memset(out,0,n*2u*sizeof *out);bb_visual_peak=0;return;
        }
        /* Retirement alone does not prove a clean audio block. */
    }
    uint32_t old_output_faults=bb_sound.output.faults;
#endif
    uint32_t faults_before=bb_audio_faults;
    bb_audio_entered=1;
    int render_ok=bb_audio_render(&bb_sound,n);
    song.playing=bb_sound.core.running;
    if(!render_ok) {
        bb_audio_render_failed=1;bb_target_render_fault_note(1,n);
        memset(out,0,n*2u*sizeof *out);bb_visual_peak=0;bb_audio_faults++;bb_audio_entered=0;return;
    }
#if BB_OUTPUT_GUARD
    /* Count sanitized invalid input, not ordinary peak reduction. */
    bb_audio_faults+=bb_sound.output.faults-old_output_faults;
#endif
#if BB_AUDIO_PROFILE
    bb_attr_begin(&bb_attr,BB_ATTR_OUTPUT_CONVERT,bb_profile_clock());
#endif
    bb_ticks=bb_sound.core.ticks;
    /* Stock transport/editor state is not the Big Beat transport. The main
     * loop owns incoming musical MIDI; OTA keeps its separate USB path. */
    transport_req=0;
    for(i=0;i<BB_MACHINES;i++) {
#if BB_WITH_RICH_DRUM_RENDER
        unsigned rich_head=bb_audio_phrase_playhead(&bb_sound,i);
        if(rich_head!=UINT32_MAX){bb_playhead[i]=(uint8_t)rich_head;continue;}
#endif
        uint8_t next=bb_sound.core.drums[i].next;
        bb_playhead[i]=(uint8_t)(next?next-1u:bb_sound.core.drums[i].length-1u);
    }
    for(i=0;i<BB_SYNTHS;i++) {
        uint8_t next=bb_sound.core.synths[i].next;
        bb_synth_playhead[i]=(uint8_t)(next?next-1u:bb_sound.core.synths[i].length-1u);
    }
    for(i=0;i<n;i++) {
        float l=bb_sound.left[i],r=bb_sound.right[i];
        /* NaN/range guard before float-to-int; master pot remains physical.
         * Conservative clamp, not a claim of finished limiter/mastering. */
        if(!(l==l && r==r)){l=r=0;bb_audio_faults++;}
        if(l>0.98f)l=0.98f;if(l< -0.98f)l= -0.98f;
        if(r>0.98f)r=0.98f;if(r< -0.98f)r= -0.98f;
        { int32_t master=fx_usb_fixed?MASTER_FULL:(int32_t)song.master_q12;
          out[2u*i]=((int32_t)(l*32767.0f)*master)/MASTER_FULL;
          out[2u*i+1u]=((int32_t)(r*32767.0f)*master)/MASTER_FULL; }
    }
#if BB_WITH_SESSION
    /* A save pauses rather than clears release tails. Ease them back from
     * silence too; loading resets tails, so this is then a silent no-op. */
    if(bb_session_resume && n) {
        unsigned fade=n;
#if BB_AUDIO_NATIVE_HALF
        if(bb_half_active && fade>32u)fade=32u;
#endif
        for(i=0;i<fade;i++)for(unsigned ch=0;ch<2;ch++)
            out[2u*i+ch]=(out[2u*i+ch]*(int32_t)(i+1u))/(int32_t)fade;
        bb_session_resume=0;
    }
    if(n){bb_session_last_output[0]=out[2u*n-2u];bb_session_last_output[1]=out[2u*n-1u];}
#endif
#if BB_AUDIO_NATIVE_HALF
    if(bb_half_active && n==128u)for(i=0;i<128u;i+=32u)bb_visual_measure(out+i*2u,32u);
    else
#endif
    bb_visual_measure(out,n);
    bb_audio_render_failed=(bb_audio_faults!=faults_before);
    bb_target_render_fault_note(bb_audio_render_failed!=0,n);
    bb_audio_entered=0;
#if BB_AUDIO_PROFILE
    bb_attr_end(&bb_attr,BB_ATTR_OUTPUT_CONVERT,bb_profile_clock(),n);
#endif
}

#if BB_AUDIO_NATIVE_HALF
#undef bb_target_audio
static void bb_target_audio(int32_t *out,uint32_t n) {
    if(!bb_half_active || n!=32u) {
        bb_half_active=0;bb_target_audio_legacy(out,n);return;
    }
    if(!bb_half_ready) {
        bb_target_audio_legacy(bb_half_output,128u);bb_half_ready=1;
    }
#if BB_AUDIO_PROFILE
    bb_attr_begin(&bb_attr,BB_ATTR_COPY,bb_profile_clock());
#endif
    memcpy(out,bb_half_output+bb_half_at*2u,n*2u*sizeof *out);
    bb_half_at+=n;
    if(bb_half_at==128u)bb_half_active=0;
#if BB_AUDIO_PROFILE
    bb_attr_end(&bb_attr,BB_ATTR_COPY,bb_profile_clock(),n);
#endif
}
#endif
