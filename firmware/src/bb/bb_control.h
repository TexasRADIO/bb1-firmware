#ifndef BB_CONTROL_H
#define BB_CONTROL_H
#include "bb_core.h"
#include "bb_pattern_limits.h"
#ifndef BB_WITH_EVOLVE
#define BB_WITH_EVOLVE 0
#endif
#ifndef BB_WITH_PARTS
#define BB_WITH_PARTS 0
#endif
#ifndef BB_WITH_PATTERN
#define BB_WITH_PATTERN 0
#endif
#ifndef BB_WITH_RICH_DRUM_HANDOFF
#define BB_WITH_RICH_DRUM_HANDOFF 0
#endif
enum {
    BB_EV_ENABLE, BB_EV_FREEZE, BB_EV_CAPTURE, BB_EV_HOME,
    BB_EV_AMOUNT, BB_EV_INTERVAL, BB_EV_PARTS, BB_EV_ATTRIBUTES,
    BB_EV_ROOT, BB_EV_SCALE, BB_EV_LOW, BB_EV_HIGH, BB_EV_VOICES,
    BB_EV_LOCK, BB_EV_RUN, BB_EV_ACTIONS
};

#define BB_COMMAND_CAPACITY 16u
enum {BB_KEY_PHYSICAL, BB_KEY_USB, BB_KEY_TRS, BB_KEY_SOURCES};
typedef enum {
    BB_CMD_TEMPO, BB_CMD_SWING, BB_CMD_TRANSPORT,
    BB_CMD_DRUM_STEP, BB_CMD_DRUM_LENGTH, BB_CMD_DRUM_MUTE,
    BB_CMD_DRUM_LEVEL, BB_CMD_DRUM_ENABLE, BB_CMD_NOTE, BB_CMD_DILLO_PATCH,
    BB_CMD_TELEGRAM_PATCH, BB_CMD_SYNTH_MUTE, BB_CMD_SYNTH_LENGTH,
    BB_CMD_SYNTH_PARAMETER, BB_CMD_DRUM_KIT, BB_CMD_DRUM_PARAMETER,
    BB_CMD_FX_PARAMETER, BB_CMD_BAND_LEVEL, BB_CMD_DRUM_TRIGGER, BB_CMD_EVOLVE,
    BB_CMD_DRUM_TOGGLE, BB_CMD_NOTE_EDIT, BB_CMD_NOTE_PITCH, BB_CMD_NOTE_SHIFT,
    BB_CMD_SYNTH_KEY, BB_CMD_SYNTH_RELEASE, BB_CMD_SYNTH_SOURCE_RELEASE,
    BB_CMD_PART_LAUNCH, BB_CMD_SYNTH_PATTERN, BB_CMD_DRUM_PATTERN,
    BB_CMD_SYNTH_TRANSPOSE, BB_CMD_TRIO_BANK, BB_CMD_909_PARAMETER,
    BB_CMD_GROUP_LEVEL, BB_CMD_GROUP_MUTE, BB_CMD_BAND_TOGGLE, BB_CMD_SWING_X2
#if BB_SAMPLE_HOST_INTEGRATION
    ,BB_CMD_SAMPLE_KIT /* value=immutable registered kit ID; all other fields zero */
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
    ,BB_CMD_DRUM_PHRASE
#endif
#if BB_WITH_RICH_DRUM_RENDER
    ,BB_CMD_DRUM_SOURCE_PARAMETER
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    ,BB_CMD_SAMPLE_MIX /* Append only: existing command numbers unchanged. */
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
    ,BB_CMD_KIT_PHRASE /* Owned phrase slot and ticket; note.gate=registered kit ID. */
#endif
} bb_command_type;
/* SAMPLE_MIX: target=0,step=sample ID,voice=BB_SAMPLE_MIX_*,value=setting;
 * note bytes encode sample-selection completion serial little endian. The
 * audio owner rejects stale IDs/serials. Not an 808 pot or a pattern edit. */
enum {BB_SAMPLE_MIX_ECHO,BB_SAMPLE_MIX_SPRING,BB_SAMPLE_MIX_GAIN,BB_SAMPLE_MIX_COUNT};
#include "bb_sample_gain.h"
/* DRUM_SOURCE_PARAMETER: explicit machine,voice=actual portable sound0..15,
 * step=parameter,value=bounded setting; four note bytes encode the owning
 * drum-family epoch little endian. Audio admission rejects foreign/stale
 * engines BEFORE core mutation. Existing command numbers stay unchanged. */
/* SYNTH_PATTERN is an internal copied-payload token, not a per-note edit.
 * Submit only through bb_audio_pattern_submit. target=synth, step=inbox slot;
 * ticket is value | (voice<<16) | (note.note<<24); other note bytes are zero.
 * Raw queue acceptance is never proof that a matching payload exists. */
/* DRUM_PATTERN uses the same scalar encoding with an explicit drum machine
 * and the separate drum inbox; submit through bb_audio_drum_pattern_submit. */
/* Explicit target: changing the selected UI machine cannot redirect a queued edit.
   Steps are zero-based. DRUM_STEP uses voice plus value bit0=hit/bit1=accent.
   NOTE uses synth index in target and copies all four note fields.
   DRUM_PARAMETER: target=machine, voice=track, step=portable parameter ID.
   DRUM_KIT is one atomic sound-only recipe recall, not a batch of commands.
   FX_PARAMETER: voice=portable effect/send ID, value=bounded setting;
   the selected instrument/page cannot redirect it.
   BAND_LEVEL: target=bb_mix_channel, value=0..127; leaves patches/notes intact.
   DRUM_TRIGGER: target=machine, voice=physical drum lane, value=1..127 velocity.
   Transient audition only: never writes steps, starts transport or enables a part.
   EVOLVE: voice=BB_EV_* action; LOCK uses target=part, step=0..63, value=0/1.
   Audio-owner event, not mirrored core state. Rejected if not compiled in.
   Queue acceptance is not operation success; inspect the audio result. */
/* Live pattern gestures operate on the audio owner's current step, not a
   stale UI copy: DRUM_TOGGLE value=0 gate / 1 accent; NOTE_EDIT voice=bb_edit,
   value=entry pitch when enabling a rest; NOTE_PITCH changes pitch only;
   NOTE_SHIFT value=delta+127 (0..254), clamped to MIDI 0..127. */
typedef struct {
    bb_command_type type;
    uint16_t value;
    uint8_t target,step,voice;
    bb_note note;
} bb_command;
/* TRIO_BANK is an owned-slot token, never an external pointer: target=initial
 * part, step=0, ticket=value|(voice<<16)|(note.note<<24). Other note bytes zero.
 * Only bb_audio_trio_publish may produce it; raw queue acceptance is not proof
 * of a prepared bank. Requires BB_WITH_PARTS. */
/* SYNTH_TRANSPOSE: target=synth, value=semitones+48 (0..96), all other
 * fields zero. A temporary audio-owner performance offset, not a note edit
 * or sound patch. Applies to sequence events from the next scheduled note;
 * held live keys/MIDI are unchanged. Stop, sound and pattern recalls retain
 * it; audio init/scene restore resets it. It is not saved in session v1. */
/* PART_LAUNCH: value=0..7. Consumer-owned whole-band part request; the UI
 * mirror does not apply it. Requires BB_WITH_PARTS; inspect parts_result.
 * Queued requests land at the next phrase; transport stop/start cancels them. */
/* SYNTH_KEY: explicit synth target, step=BB_KEY_* source (zero=physical),
 * voice=MIDI note, value=0..127 velocity
 * (0 releases). SYNTH_RELEASE: target=synth, value=0, releases every held key.
 * Transient audio-owned events, never pattern edits. A producer MUST handle
 * a rejected release (retry or a separate guaranteed panic path) before this
 * API is connected to hardware/MIDI; the bounded FIFO cannot promise delivery.
 * SYNTH_SOURCE_RELEASE releases only step's source on target, value=0.
 * Duplicate ons replace that source's ownership, not a reference count.
 * The latest on determines a pitch's articulation until its final owner lets go. */
typedef uintptr_t (*bb_enter_fn)(void *);
typedef void (*bb_leave_fn)(void *,uintptr_t);
typedef struct {
    bb_command items[BB_COMMAND_CAPACITY];
    bb_enter_fn enter;
    bb_leave_fn leave;
    void *lock_context;
    unsigned read,write,count;
    uint8_t stop_pending,stop_inflight; /* isolated STOP-lane proposal */
#if BB_SAMPLE_HOST_INTEGRATION
    /* Cold-bound host prototype admission hook, audio consumer only, outside
     * queue lock and before core mutation. STOP bypasses admission. */
    int (*admit)(void *,const bb_command *);
    void *admit_context;
#endif
} bb_commands;
/* Caller holds queue guard or exclusive suspended ownership. Not an atomic
 * snapshot API; NULL cannot establish idle ownership. Capacity checks still
 * use count, while save/switch/resync guards use this complete predicate. */
static inline int bb_commands_idle(const bb_commands *q) {
    return q && !q->count && !q->stop_pending && !q->stop_inflight;
}

/* NULL hooks are for a single-threaded host ONLY. On target, supply bounded
   IRQ-save/restore hooks with compiler barriers, on the same CPU as audio.
   This is NOT a lock-free or cross-core queue. Never use a blocking mutex from
   an audio interrupt. Hooks and queue lifetime must outlive every producer. */
int bb_commands_init(bb_commands *,bb_enter_fn,bb_leave_fn,void *);
int bb_command_push(bb_commands *,const bb_command *);
/* Invalid/full returns 0 without changing queue or core. UI must visibly reject
   or retry; never silently discard a failed stop, edit or patch change. */
unsigned bb_apply_pending(bb_core *,bb_commands *);
typedef void (*bb_live_command_fn)(void *,const bb_command *);
/* Same bounded FIFO drain, with audio-owner events delivered in command order
   outside the queue lock. Callback runs on the audio consumer, never the UI.
   NULL callback discards events (for the UI's state-only mirror). Transport
   and synth mute also notify after updating core state, so the audio owner can
   release held keys and reset evolution phrase cadence on stop/restart.
   Do not re-enter this consumer from the callback. New producer commands wait
   for the next drain. All accepted hits occur at this audio block's start. */
unsigned bb_apply_pending_live(bb_core *,bb_commands *,bb_live_command_fn,void *);
/* D1 (reliability r1): drain for an audio owner whose output stays off until
 * reboot. Same entry snapshot and locking; START is refused; the transport
 * always ends stopped; DRUM_KIT/DILLO/TELEGRAM sound loads are dropped (the
 * picker reports AUDIO FAULT); other core commands apply (a save records
 * them); audio-owner events go to `live`, which must not render. */
unsigned bb_apply_pending_faulted(bb_core *,bb_commands *,bb_live_command_fn,void *);
unsigned bb_process_controlled(bb_core *,bb_commands *,uint32_t,
                               bb_hit_fn,bb_note_fn,bb_render_fn,void *);
#endif

/* 909_PARAMETER: target=layer, voice=actual 909 voice or kit=11,
 * step=descriptor ordinal, value=bounded pot. The four note bytes carry a
 * little-endian owner activation epoch, NOT notes or engine pointers. */
