#ifndef BB_PATTERN_H
#define BB_PATTERN_H
#include "bb_core.h"
#include "bb_pattern_limits.h"
#include "bb_note_expression.h"
enum { BB_PATTERN_NONE, BB_PATTERN_QUEUED, BB_PATTERN_APPLIED,
       BB_PATTERN_CANCELLED, BB_PATTERN_REJECTED };
enum { BB_PATTERN_SLOT_FREE, BB_PATTERN_SLOT_WRITING, BB_PATTERN_SLOT_QUEUED };
typedef struct {
    bb_note notes[BB_STEPS]; uint8_t length;
    bb_note_expression expression[BB_STEPS];
} bb_pattern_data;
typedef struct {
    bb_pattern_data data;
    uint32_t ticket;
    uint8_t synth, phrase;
    volatile uint8_t state;
} bb_pattern_slot;
typedef struct {
    bb_pattern_data data;
    uint32_t ticket;
    uint8_t phrase, ready;
} bb_pattern_pending;
typedef struct {
    uint32_t seen, queued, applied, launches;
    uint64_t applied_frame;
    uint8_t result;
} bb_pattern_status;
typedef struct {
    bb_pattern_slot inbox[BB_PATTERN_SLOTS];
    bb_pattern_pending pending[BB_SYNTHS];
    bb_pattern_status status[BB_SYNTHS];
    uint32_t next_ticket;
    uint8_t position, ready; /* phase modulo192, independent of uint32 tick wrap */
    bb_note_expression expression[BB_SYNTHS][BB_STEPS]; /* audio-owned applied sidecar */
    uint32_t expression_errors;
} bb_pattern_handoff;
/* Separate appended drum inbox preserves every existing synth/DSP offset.
 * Tickets share patterns.next_ticket so stale tokens cannot cross kinds. */
typedef struct {
    bb_drum_step steps[BB_STEPS];
    uint16_t mode_mask,mode_value;
    uint8_t length,family; /* 0=808 lanes, 1=projected 909 lanes */
    uint8_t apply_modes; /* explicit opt-in; zero preserves pattern-only semantics */
} bb_drum_pattern_data;
enum {BB_DRUM_PATTERN_ERROR_NONE,BB_DRUM_PATTERN_ERROR_TOKEN,
      BB_DRUM_PATTERN_ERROR_DATA,BB_DRUM_PATTERN_ERROR_MODE,BB_DRUM_PATTERN_ERROR_FAMILY
#if BB_SAMPLE_HOST_INTEGRATION
      ,BB_DRUM_PATTERN_ERROR_SAMPLE
#endif
};
/* D1 (reliability r1): fixed value, independent of the optional SAMPLE id. */
enum {BB_DRUM_PATTERN_ERROR_FAULT=15};
typedef struct {
    bb_pattern_status state;
    uint8_t error;
} bb_drum_pattern_status;
typedef struct {
    bb_drum_pattern_data data;
    uint32_t ticket;
    uint32_t family_epoch;
    uint8_t machine,phrase;
    volatile uint8_t state;
} bb_drum_pattern_slot;
typedef struct {
    bb_drum_pattern_data data;
    uint32_t ticket;
    uint32_t family_epoch;
    uint8_t phrase,ready;
    uint16_t expected_modes; /* audio-owner modes when request consumed */
} bb_drum_pattern_pending;
typedef struct {
    bb_drum_pattern_slot inbox[BB_PATTERN_SLOTS];
    bb_drum_pattern_pending pending[BB_MACHINES];
    bb_drum_pattern_status status[BB_MACHINES];
    uint32_t family_epoch[BB_MACHINES];
    uint8_t family[BB_MACHINES];
} bb_drum_pattern_handoff;
#endif
