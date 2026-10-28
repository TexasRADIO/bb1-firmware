/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_AUDIO_PHRASE_H
#define BB_AUDIO_PHRASE_H
#include "bb_phrase_index.h"
#include "bb_pattern.h"
#include "bb_control.h"
#include "group_gain.h"
/* Representation may carry a sampled ride; the808 renderer still cannot.
 * Actual engine capability must be checked again at publication/application. */
#define BB_RICH_808_NATIVE_VOICES (UINT32_C(65535)|(UINT32_C(1)<<BB_PHRASE_SHAKER))
/* Effective playback includes default substitutions; native identity stays
 * separate so a substituted source is never advertised as an original voice. */
#define BB_RICH_808_VOICES (BB_RICH_808_NATIVE_VOICES|UINT32_C(0x70000))
#define BB_RICH_SOURCE_VOICES (BB_RICH_808_VOICES|(UINT32_C(1)<<BB_FDV_RD))
/* Structural rich-phrase range only. These two sources are playable solely
 * when the audio owner has a validated auxiliary sample binding. */
#define BB_RICH_AUX_VOICES ((UINT32_C(1)<<BB_FDV_SY)|(UINT32_C(1)<<BB_FDV_ZAP))
#define BB_RICH_REPRESENTABLE_VOICES (BB_RICH_SOURCE_VOICES|BB_RICH_AUX_VOICES)
#ifndef BB_WITH_RICH_DRUM_HANDOFF
#define BB_WITH_RICH_DRUM_HANDOFF 0
#endif
#ifndef BB_WITH_RICH_DRUM_RENDER
#define BB_WITH_RICH_DRUM_RENDER 0
#endif
#if BB_WITH_RICH_DRUM_RENDER && (!BB_WITH_RICH_DRUM_HANDOFF || !BB_USE_DRUM_SPAN || BB_OWNER_AUDIO_BRIDGE || BB_WITH_PARTS || !BB_SAMPLE_HOST_INTEGRATION)
#error "Rich renderer requires copied handoff, CPU0 span rendering and no combined bank loader"
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
#if defined(BB_RICH_TARGET_QUALIFY)
/* Resource/stack qualification only, never release qualification. The target
 * builder must still enforce reserves and package/device acceptance gates. */
#if !defined(BB_GROUP_TARGET_CANDIDATE) || !defined(BB_SAMPLE_TARGET_CANDIDATE) || !BB_WITH_RICH_DRUM_RENDER || BB_OWNER_AUDIO_BRIDGE || BB_WITH_PARTS || !BB_USE_DRUM_SPAN
#error "Rich target qualification requires exclusive CPU0 span ownership and copied rendering"
#endif
#elif !defined(BB_GROUP_HOST_SERIAL_TEST) || defined(BB_GROUP_TARGET_CANDIDATE)
#error "Rich phrase handoff is host integration only: rendering/edit/save/target gates remain"
#endif
enum {BB_RICH_DRUMS=2,BB_RICH_PINNED=3};
enum {BB_RICH_NONE,BB_RICH_QUEUED,BB_RICH_STAGED,BB_RICH_CANCELLED,BB_RICH_REJECTED,BB_RICH_APPLIED};
enum {BB_RICH_ERROR_NONE,BB_RICH_ERROR_TOKEN,BB_RICH_ERROR_DATA,BB_RICH_ERROR_ENGINE,
      BB_RICH_ERROR_FAULT /* D1: ended by a non-retirable audio fault */};
typedef struct {
    bb_phrase_index data;
    uint32_t ticket,epoch;
    uint8_t machine,phrase,kit;
    volatile uint8_t state;
} bb_rich_slot;
typedef struct {
    bb_rich_slot inbox[BB_PATTERN_SLOTS];
    struct {uint32_t ticket;uint8_t slot,phrase,ready,kit;} pending[BB_RICH_DRUMS];
    bb_phrase_index active[BB_RICH_DRUMS];
    bb_pattern_status status[BB_RICH_DRUMS];
    uint8_t valid[BB_RICH_DRUMS],error[BB_RICH_DRUMS];
#if BB_WITH_RICH_DRUM_RENDER
    struct {
        bb_delayed queue;
        uint32_t phase,generation,primary,delayed;
        uint8_t step,pending,odd,swing,sounded;
    } player[BB_RICH_DRUMS];
    uint32_t render_errors,edit_refusals;
#endif
} bb_audio_phrase_state;
struct bb_audio;
/* Audio-owner/IRQ-guard/exclusive-hold only. No lock is acquired here. */
uint32_t bb_audio_phrase_source_voices_owned(const struct bb_audio *,unsigned);
/* Main-loop snapshot; bounded existing command guard, no sample-data reads. */
uint32_t bb_audio_phrase_source_voices(struct bb_audio *,unsigned);
typedef struct {bb_pattern_status state;uint8_t valid,error;} bb_audio_phrase_status;
/* Main-loop-owned immutable source view. Initialize to zero and reset after
 * audio reinitialization. Never a writable projection back into the DSP. */
typedef struct {
    bb_phrase phrase;
    bb_phrase_controls controls;
    uint32_t applied,generation;
    uint8_t valid;
} bb_phrase_view;
typedef struct {uint8_t hits,accents,soft,flams,repeats;} bb_phrase_cell;
/* 0 refused, 1 unchanged, 2 refreshed. One bounded command lock; no decoding
 * or catalogue access while held. Only a changed immutable phrase is copied. */
int bb_audio_phrase_view_read(struct bb_audio *,unsigned,bb_phrase_view *);
/* Main-owned zero-initialized sound snapshot. 0 invalid,1 queue pending (no
 * sound copy),2 authoritative idle snapshot refreshed,3 unchanged. Copying
 * portable pots is bounded; no DSP/oscillator state crosses the boundary. */
typedef struct {
    bb_drum_sound sound;
    uint32_t revision,epoch;
    uint8_t valid,supported,pending;
} bb_source_tone_view;
int bb_audio_source_tone_view_read(struct bb_audio *,unsigned,bb_source_tone_view *);
/* Exact selected source voice, zero-based step. Duplicate and delayed-strike
 * indicators retain their own counts; no collapse into an editable 808 grid. */
int bb_phrase_view_cell(const bb_phrase_view *,unsigned,unsigned,bb_phrase_cell *);
/* Handoff-only profile acknowledges STAGED; optional rich renderer acknowledges
 * APPLIED musical state (stopped loads do not sound until PLAY). Host-only and
 * kept out of public browsers until rich editing/save/target gates are complete. */
int bb_audio_phrase_submit(struct bb_audio *,unsigned,const bb_phrase_index *,unsigned,uint32_t *);
#if BB_SAMPLE_HOST_INTEGRATION
/* Control-thread submission: one immutable kit + phrase ticket. The audio
 * owner validates again and swaps both at the same phrase boundary. */
int bb_audio_kit_phrase_submit(struct bb_audio *,unsigned,unsigned,
                               const bb_phrase_index *,unsigned,uint32_t *);
#endif
int bb_audio_phrase_status_read(struct bb_audio *,unsigned,bb_audio_phrase_status *);
/* One locked snapshot: request acknowledgements from the selected producer,
 * playing identity from the actual active legacy/rich owner. */
int bb_audio_drum_selection_status(struct bb_audio *,unsigned,unsigned,bb_drum_pattern_status *);
void bb_audio_phrase_command(struct bb_audio *,const bb_command *);
void bb_audio_phrase_cancel(struct bb_audio *);
/* D1 (reliability r1), audio owner only, while output is off until reboot:
 * end a queued/pending phrase as REJECTED with BB_RICH_ERROR_FAULT. */
void bb_audio_phrase_fault_command(struct bb_audio *,const bb_command *);
void bb_audio_phrase_fault(struct bb_audio *);
void bb_audio_phrase_tick(struct bb_audio *,unsigned);
void bb_audio_phrase_cancel_pending(struct bb_audio *,unsigned);
#if BB_WITH_RICH_DRUM_RENDER
void bb_audio_phrase_restart(struct bb_audio *,unsigned);
void bb_audio_phrase_transport(struct bb_audio *,unsigned);
void bb_audio_phrase_legacy(struct bb_audio *,unsigned);
int bb_audio_phrase_admit(void *,const bb_command *);
int bb_audio_phrase_render(struct bb_audio *,unsigned,gg_state *,float *,float *,float *,unsigned,uint64_t);
int bb_audio_phrase_preflight(const bb_phrase_index *);
/* Same complete admission proof with the caller's actual voice capabilities.
 * Avoid a second full index walk merely to narrow UINT32_MAX afterwards. */
int bb_audio_phrase_preflight_playable(const bb_phrase_index *,uint32_t);
/* Audio-owner cursor after the most recently rendered sample; read/copy only
 * under that ownership or the command lock. UINT32_MAX means no rich owner. */
unsigned bb_audio_phrase_playhead(const struct bb_audio *,unsigned);
/* Only after the real suspended scene restore reset the entire audio owner.
 * Caller keeps both producers/audio paused throughout this cold install. */
int bb_audio_phrase_scene_install_reset(struct bb_audio *,const bb_phrase [2],unsigned);
#endif
#endif
#endif
