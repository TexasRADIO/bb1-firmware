#ifndef BB_AUDIO_H
#define BB_AUDIO_H
#if !defined(BB_GROUP_HOST_SERIAL_TEST) && !defined(BB_GROUP_TARGET_CANDIDATE)
#error "Group audio bridge is host-only pending worker, persistence and resource integration"
#endif
#include "group_gain.h"
#ifndef BB_SAMPLE_HOST_INTEGRATION
#define BB_SAMPLE_HOST_INTEGRATION 0
#endif
#if BB_SAMPLE_HOST_INTEGRATION
#if defined(BB_SAMPLE_DUAL_TRIAL) && (!defined(BB_GROUP_HOST_SERIAL_TEST) || defined(BB_GROUP_TARGET_CANDIDATE)) && !defined(BB_SAMPLE_DUAL_NATIVE_MEASURE)
#error "Dual sample owner requires host qualification before target integration"
#endif
/* BB_SAMPLE_DUAL_NATIVE_MEASURE is an offline compiler/resource profile only.
 * It confers no package, installation, timing or release qualification. */
#define BB_SAMPLE_808_ID 254u /* disjoint from sample IDs and direct-binding255 */
#if defined(BB_SAMPLE_HYBRID_TRIAL) && defined(BB_GROUP_TARGET_CANDIDATE) && !defined(BB_SAMPLE_HYBRID_QUALIFY)
#error "Hybrid target requires explicit qualification build; not a released profile"
#endif
#if defined(BB_SAMPLE_TARGET_CANDIDATE)
#if !defined(BB_GROUP_TARGET_CANDIDATE) || BB_OWNER_AUDIO_BRIDGE || BB_WITH_909_ROUTE || !BB_ONE_DRUM_STUDIO
#error "Native sample candidate requires exclusive single-drum CPU0 ownership"
#endif
#elif !defined(BB_GROUP_HOST_SERIAL_TEST) || defined(BB_GROUP_TARGET_CANDIDATE)
#error "Sample backend is host integration only until storage and target qualification"
#endif
#include "bb_sample_buses_r1.h"
#include "bb_aux_voices_r1.h"
typedef struct {
    bb_sample_slot *slots;
    const bb_sample_zone *zones;
    unsigned count,nz;
    uint8_t lane_notes[11]; /* explicit current-sequencer mapping; not extra-role support */
    unsigned echo,spring;
    float gain;
} bb_sample_audio_binding;
/* Per-kit differences only. Voice ownership, mix gain and FX sends belong to
 * the current binding and must not be restored from a preset snapshot. */
typedef struct {
    const bb_sample_zone *zones;
    uint8_t nz,lane_notes[11];
    uint32_t source_voices;
} bb_sample_catalogue_entry;
#endif
#include "bb_control.h"
#if BB_SAMPLE_HOST_INTEGRATION
_Static_assert(BB_DRUM_TRACKS==11,"Review sample trial lane map when schema changes");
#endif
#include "bb_scope.h"
#include "bb_trio.h"
enum {BB_TRIO_FREE,BB_TRIO_WRITING,BB_TRIO_QUEUED_SLOT,BB_TRIO_PINNED};
#ifndef BB_TRIO_EXTERNAL_PROGRAM
#define BB_TRIO_EXTERNAL_PROGRAM 0
#endif
enum {BB_TRIO_NONE,BB_TRIO_QUEUED,BB_TRIO_APPLIED,BB_TRIO_CANCELLED,BB_TRIO_REJECTED};
typedef struct {uint32_t seen,queued,applied,launches;uint64_t applied_frame;uint8_t result;} bb_trio_status;
#include "bb_transpose.h"
#if BB_WITH_PATTERN
#include "bb_pattern.h"
#include "bb_audio_phrase.h"
#endif
#if BB_WITH_PARTS
#ifndef BB_WITH_PREPARED_BANK
#define BB_WITH_PREPARED_BANK 0
#endif
#if BB_WITH_PREPARED_BANK
#include "prepared_bank.h"
#else
#include "bb_parts.h"
#endif
#endif
#if BB_WITH_EVOLVE
#include "bb_evolve.h"
enum {BB_EV_RESULT_NONE,BB_EV_RESULT_OK,BB_EV_RESULT_WAIT,
      BB_EV_RESULT_REJECTED,BB_EV_RESULT_CONFLICT,BB_EV_RESULT_CANCELLED};
#endif
#include "drum808.h"
#ifndef BB_USE_DRUM_SPAN
#define BB_USE_DRUM_SPAN 0
#endif
#ifndef BB_OUTPUT_GUARD
#define BB_OUTPUT_GUARD 0
#endif
#ifndef BB_WITH_EFFECTS
#define BB_WITH_EFFECTS 0
#endif
#if BB_WITH_EFFECTS
#include "bb_echo.h"
#include "bb_spring.h"
#endif
#if BB_OUTPUT_GUARD
#include "bb_output.h"
#endif
#ifndef BB_WITH_DILLO
#define BB_WITH_DILLO 0
#endif
#if BB_WITH_DILLO
#include "bb_dillo.h"
#endif
#ifndef BB_WITH_TELEGRAM
#define BB_WITH_TELEGRAM 0
#endif
#if BB_WITH_TELEGRAM
#include "bb_telegram.h"
#endif
/* First target sound adapter: four independent X0X/8W8 reference 808 slots,
 * two enabled by default. Four-slot host support is not target CPU acceptance.
 * Same code runs on the desktop and the FM-1. No target/graphics dependency.
 * Rendering preserves one-sample reference DSP semantics inside event spans. */
typedef struct bb_audio {
    bb_core core;
    bb_commands commands;
#if BB_WITH_PARTS
#if BB_WITH_PREPARED_BANK
    prepared_bank parts; /* compact scenes plus bounded main-loop preparation */
#else
    bb_parts parts; /* lazy audio-owned snapshot; UI never writes this bank */
#endif
    uint8_t parts_result,parts_error;
#if BB_WITH_EFFECTS
    unsigned wet_offset; /* prefix already processed before a mid-block launch */
#endif
#endif
#if BB_WITH_DILLO || BB_WITH_TELEGRAM
    /* Last-note priority, all 128 pitches; unique entries, no heap/allocation.
     * Transient, not scene data. Audio consumer owns every write. */
    struct {uint8_t order[128],velocity[128],owners[128];uint16_t count;} keys[BB_SYNTHS];
#endif
#if BB_WITH_EVOLVE
    bb_evolve evolve; /* lazy snapshot on first audio-owned evolution command */
#if BB_WITH_PATTERN
    /* Evolution changes a step's pitch/gate/accent/slide in place, not its
     * velocity/gate-length phrasing. Authored expression edits are conflicts
     * until explicitly captured, just like authored note edits. */
    bb_note_expression evolve_expression[BB_SYNTHS][BB_STEPS];
#endif
    uint8_t evolve_home_pending,evolve_result;
#endif
    drum808_t kit[BB_MACHINES];
#if BB_USE_DRUM_SPAN
    /* Reused by each kit, in the audio pool rather than the interrupt stack.
     * Sends remain separate for the forthcoming shared effects bus. */
    float drum_dry[128], drum_rev[128], drum_dly[128];
#endif
#if BB_WITH_DILLO
    bb_dillo dillo;
    float bass[128];
#endif
#if BB_WITH_TELEGRAM
    bb_telegram telegram;
    float lead[128];
#endif
    float left[128], right[128];
#if BB_WITH_EFFECTS
    bb_echo echo;
    bb_spring spring;
    bb_fx_settings applied_effects;
    /* Shared sends collected at sequencer spans; process wet returns once
     * after the complete block, before the common output guard. */
    float echo_send[128],spring_send[128],wet_left[128],wet_right[128];
#endif
    uint32_t hits[BB_MACHINES], spans;
    /* Exact endpoint, bounded audio-rate ramp. 0..65536 is silence..unity. */
    uint32_t mix_gain[BB_MACHINES];
    uint32_t band_gain[BB_MIX_CHANNELS];
    /* One reused curve, filled only while a fader moves. The drum bus ramp
     * advances once per span, not once per enabled kit. */
    float band_curve[128];
    uint32_t drum_revision[BB_MACHINES];
#if BB_OUTPUT_GUARD
    bb_output output;
#endif
    unsigned offset;
    bb_scope scope;
#if BB_WITH_PATTERN
    /* Append optional inbox state: retain every existing DSP/evolution offset
     * in the target profile with parts disabled. */
    bb_pattern_handoff patterns;
    bb_drum_pattern_handoff drum_patterns;
#endif
    /* Append transient performance state, retaining prior DSP field offsets.
     * Audio consumer writes; UI must use the existing queue/short lock.
     * Session v1 stores source notes, not this temporary pitch offset. */
    int8_t transpose[BB_SYNTHS];
#if BB_WITH_PARTS
    struct {
#if BB_TRIO_EXTERNAL_PROGRAM
        /* Bind a fixed buffer only after its boot-only lifetime has ended.
         * It remains exclusively owned until audio is safely suspended. */
        bb_trio_program *program;
#else
        bb_trio_program program;
#endif
        bb_trio_status status;
        uint32_t ticket,next_ticket,check;
        uint8_t phrase,position;
        volatile uint8_t state;
    } trio;
#endif
    /* Append audio-owned transient faders; no shifted existing DSP offsets. */
    gg_state *groups; /* startup-bound external audio-owned state */
#if BB_SAMPLE_HOST_INTEGRATION
    bb_sample_audio_binding *sample;
    bb_aux_voices *aux; /* cold-bound shared SY/ZAP one-shots, never a kit */
#if defined(BB_SAMPLE_DUAL_TRIAL)
    bb_sample_audio_binding *sample_b;
    uint8_t sample_b_selected,sample_b_requested,sample_b_select_result;
    uint32_t sample_b_select_serial;
#endif
    uint32_t sample_rejected;
    bb_sample_catalogue_entry sample_catalogue[8]; /* compact cold-copied descriptors */
    uint16_t sample_catalogue_lanes[8]; /* prevalidated supported-lane masks */
    uint8_t sample_catalogue_count,sample_selected,sample_requested,sample_select_result;
    uint32_t sample_select_serial; /* consumer completion count, not queue acceptance */
    uint32_t sample_edit_rejected;
    uint8_t sample_edit_rejected_type,sample_edit_rejected_lane;
#endif
#if BB_WITH_PATTERN && BB_WITH_RICH_DRUM_HANDOFF
    bb_audio_phrase_state rich;
#endif
} bb_audio;
#if BB_SAMPLE_HOST_INTEGRATION
int bb_audio_sample_bind(bb_audio *,bb_sample_audio_binding *);
/* Cold-only after validated pack bind. No capability is published merely by
 * the presence of a fourth BBSK bank; audio must own this exact pool. */
int bb_audio_aux_bind(bb_audio *,bb_aux_voices *);
uint32_t bb_audio_aux_capabilities(const bb_audio *);
bb_sample_audio_binding *bb_audio_sample_layer(const bb_audio *,unsigned);
unsigned bb_audio_sample_layer_id(const bb_audio *,unsigned);
#if BB_WITH_RICH_DRUM_HANDOFF
/* Audio-owner only: commit the registered binding for a prevalidated incoming
 * phrase. This intentionally does not inspect the outgoing active phrase. */
int bb_audio_sample_pair_commit_owned(bb_audio *,unsigned,unsigned,uint32_t);
#endif
#if defined(BB_SAMPLE_DUAL_TRIAL)
int bb_audio_sample_bind_b(bb_audio *,bb_sample_audio_binding *,unsigned);
#if BB_WITH_RICH_DRUM_RENDER
/* Cold paused restore: validates replacement phrase, preserves dormant notes.
 * Does not install phrase; keep pause until scene_install_reset succeeds. */
int bb_audio_sample_bind_b_phrase(bb_audio *,bb_sample_audio_binding *,unsigned,const bb_phrase *);
/* Paused preflight permits existing tails; does not reset either owner. */
int bb_audio_sample_restore_check_layer(bb_audio *,const bb_core *,bb_sample_audio_binding *,
                                      const bb_sample_audio_binding *,unsigned,unsigned,unsigned,const bb_phrase *);
#endif
#endif
/* Cold setup only. Copies binding descriptors; referenced zone arrays and bytes
 * must remain immutable and alive for the entire registered catalogue lifetime.
 * Results: 0=no request, 1=applied, 2=rejected; read through an owner snapshot.
 * The caller must not read audio-owned result fields concurrently on target. */
int bb_audio_sample_catalogue_bind(bb_audio *,const bb_sample_audio_binding *,unsigned);
/* Exclusive cold boot, all-or-nothing; rejects an incompatible starting pattern.
 * The current binding must be separate from the input descriptor array. */
int bb_audio_sample_startup(bb_audio *,bb_sample_audio_binding *,
                          const bb_sample_audio_binding *,unsigned count,unsigned initial);
/* Exclusive suspended owner only. Cold validation for a later scene reset;
 * does not mutate audio or sample memory and allows currently ringing tails.
 * All descriptors/sample bytes must remain immutable through commit. */
int bb_audio_sample_restore_check(bb_audio *,const bb_core *,bb_sample_audio_binding *,
                                 const bb_sample_audio_binding *,unsigned,unsigned);
#if BB_WITH_RICH_DRUM_RENDER
/* Exclusive scene hold only. Validate the replacement phrase instead of the
 * dormant legacy lanes; keep the hold until scene_install_reset succeeds.
 * Neither function changes legacy notes or installs the replacement phrase. */
int bb_audio_sample_restore_check_phrase(bb_audio *,const bb_core *,bb_sample_audio_binding *,
                                 const bb_sample_audio_binding *,unsigned,unsigned,const bb_phrase *);
int bb_audio_sample_startup_phrase(bb_audio *,bb_sample_audio_binding *,
                                 const bb_sample_audio_binding *,unsigned,unsigned,const bb_phrase *);
#endif
typedef struct {
    uint32_t serial,edit_rejections;
    uint16_t required_lanes,supported_lanes;
    uint8_t count,selected,requested,result,compatible,queue_idle,stop_pending;
    uint8_t echo,spring,level,native_compatible,machine;
    float gain; /* Actual binding values, not optimistic UI settings. */
} bb_sample_status;
/* Single-core existing queue/IRQ guard; paired hooks required on hardware.
 * NULL hooks only for exclusive host use. No pointers/sample bytes exposed.
 * Compatible is a bitset of catalogue IDs for current stored steps, not
 * a promise about queued future patterns or all voices in an original machine. */
int bb_audio_sample_status(bb_audio *,bb_sample_status *);
int bb_audio_sample_status_layer(bb_audio *,unsigned,bb_sample_status *);
/* Pure bounded command builder from an authoritative idle status snapshot.
 * ECHO/SPRING are 0..127, GAIN is 1..100 percent (saved gain stays positive;
 * the existing layer fader provides silence). Caller still handles FIFO
 * refusal; acceptance is not application. No pointers/sample bytes copied. */
int bb_audio_sample_mix_command(const bb_sample_status *,unsigned,unsigned,bb_command *);
/* Host-only prototype. Exclusive audio-consumer ownership at block boundary;
 * not thread-safe, not a UI setter. Caller retains immutable bank bytes and
 * descriptors, including retired bytes until all referencing voices finish.
 * Rejected selections leave binding/voices/core untouched. No queued pattern
 * compatibility is claimed; copied-payload control integration remains pending. */
int bb_audio_sample_select_owned(bb_audio *,const bb_sample_audio_binding *);
#endif
int bb_audio_init(bb_audio *,bb_enter_fn,bb_leave_fn,void *);
/* Requires 1..128 frames. Invalid calls produce no writes and return 0. */
int bb_audio_render(bb_audio *,unsigned frames);
/* D1 (reliability r1): audio owner only, INSTEAD of bb_audio_render while a
 * latched worker fault cannot be retired and CPU1 is verified held. Renders
 * nothing and touches no CPU1 state; drains the command FIFO, ends every
 * queued/pending kit, phrase, pattern and sample request with a visible
 * failure, refuses START and leaves the transport stopped. Returns commands. */
unsigned bb_audio_fault_drain(bb_audio *);
/* Timing P1b instruments (boot39 rebase). Audio owner only (CPU0).
 * bb_audio_render ORs these bits for every sequencer step boundary it runs;
 * the target's DMA-half timing tail reads and clears them, so hardware can
 * report the whole-half peak split by "drum B launched at this boundary".
 * Launch = a B pattern or phrase was due at that boundary: exactly the class
 * that P1b leaves on the per-span two-job path. */
#define BB_BOUNDARY_SEEN 1u
#define BB_BOUNDARY_B_LAUNCH 2u
extern uint32_t bb_audio_boundary_flags;
typedef struct {
    uint32_t compiled,enable,call_jobs,joins_saved,guard_trips,fallbacks;
    uint32_t guard_first_mask; /* first trip: bit 31 set; bits 0-13 inputs, 14 player, 15 groups, 16 structure */
    uint32_t guard_discards;   /* trips whose in-flight job was joined, verified and dropped (CPU1 kept) */
    uint32_t guard_cpu1_faults;/* trips whose join/verify failed: a real CPU1 fault, latched and retired */
} bb_p1b_report;
/* Copies the P1b counters; compiled=0 and all zero when P1b is compiled out. */
void bb_audio_p1b_report(bb_p1b_report *);
void bb_audio_demo(bb_core *);
#if BB_WITH_PATTERN
/* Main-loop producer, same CPU as audio, one producer of pattern payloads.
 * Stable caller-owned source, outside this audio object; no catalogue decoding
 * or DSP calls in this API. Two owned inbox slots, no heap or blocking wait.
 * Payload copies/validation happen outside IRQ masking. Target must supply
 * queue IRQ-save/restore hooks with compiler barriers; NULL is host-only.
 * phrase=16/32/48/64 global steps. Success copies the complete pattern and
 * returns a nonzero ticket; it means REQUEST ACCEPTED, not pattern applied.
 * Failure changes neither core nor the optional output ticket. */
int bb_audio_pattern_submit(bb_audio *,unsigned synth,const bb_pattern_data *,unsigned phrase,uint32_t *ticket);
/* Copy actual audio-consumer status under the same short critical section.
 * Out must be caller-owned. Tickets become invalid after full reinitialization.
 * Clock, sound, mute, faders, effects and live key ownership are not recalled. */
int bb_audio_pattern_status(bb_audio *,unsigned synth,bb_pattern_status *out);
/* Consumer-only integration hooks, not concurrent UI APIs. */
void bb_audio_pattern_command(bb_audio *,const bb_command *);
void bb_audio_pattern_tick(bb_audio *);
void bb_audio_pattern_transport(bb_audio *);
void bb_audio_pattern_cancel(bb_audio *,int clear_applied_labels);
/* Same ownership, copy and phrase-boundary contract as synth patterns.
 * Kit lane modes are checked by the consumer at application, not from a
 * stale UI mirror. A mismatch rejects the complete pattern, never the kit. */
int bb_audio_drum_pattern_submit(bb_audio *,unsigned machine,const bb_drum_pattern_data *,unsigned phrase,uint32_t *ticket);
int bb_audio_drum_pattern_status(bb_audio *,unsigned machine,bb_drum_pattern_status *out);
void bb_audio_drum_pattern_command(bb_audio *,const bb_command *);
void bb_audio_drum_pattern_tick(bb_audio *,unsigned position);
void bb_audio_drum_pattern_cancel(bb_audio *,int clear_applied_labels);
#endif
#if BB_WITH_EVOLVE
/* Internal consumer hooks: never call from a concurrent UI producer. */
void bb_audio_evolve_command(bb_audio *,const bb_command *);
int bb_audio_evolve_matches(const bb_audio *);
void bb_audio_evolve_tick(void *,bb_core *);
void bb_audio_evolve_stopped(bb_audio *);
#endif
#endif

#if !defined(BB_GROUP_EXTERNAL_STORAGE_TEST) && !defined(BB_GROUP_TARGET_CANDIDATE)
#error "External group storage is host-only pending phase and stack integration"
#endif
int bb_audio_groups_bind(bb_audio *,gg_state *,unsigned);
int bb_audio_groups_outside(const bb_audio *,const void *,size_t);
#if BB_SAMPLE_HOST_INTEGRATION && BB_WITH_RICH_DRUM_RENDER
int bb_audio_sample_phrase_hit(bb_audio *,unsigned,float);
int bb_audio_sample_phrase_hit_layer(bb_audio *,unsigned,unsigned,float);
uint32_t bb_audio_sample_source_voices(const bb_audio *,unsigned);
#endif
