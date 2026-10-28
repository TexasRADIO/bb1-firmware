#ifndef BB_ECHO_H
#define BB_ECHO_H
#include <stdint.h>
/* Original Big Beat three-head tape-style candidate, not a circuit model.
 * Full-rate float storage: 128 KiB, no heap or resampling. The longest head
 * is limited to the ring's capacity at the actual rate (about 743 ms/44.1k).
 * Feedback comes from the shortest head; all passes see input coloration.
 * Control changes preserve the tape and slew delay continuously. */
#define BB_ECHO_RING 32768u
typedef struct {
    float tape[BB_ECHO_RING];
    float delay, target_delay, hp_x, hp_y, low, wow, phase;
    float hp_pole, low_coeff, feedback, drive, wear, wet;
    uint32_t sample_rate, write, invalid_frames;
    uint8_t heads;
    float motion, motion_target, motion_slew;
    float head_offset[3], head_target[3], pan_offset[3], pan_target[3];
    uint32_t motion_rng, motion_remaining, motion_changes;
    /* Monotonic until init: once excited, always render all feedback tails. */
    uint32_t excited;
} bb_echo;
int bb_echo_init(bb_echo *, unsigned sample_rate);
/* Values are bounded and validated before any state mutation. Longest-head
 * delay 10..700 ms; feedback 0..90, tone/drive/wear/wet 0..100, mask 0..7.
 * Delay beyond the rate-dependent storage ceiling is clamped deliberately. */
int bb_echo_set(bb_echo *, unsigned ms, unsigned feedback, unsigned tone,
                unsigned drive, unsigned wear, unsigned wet, unsigned heads);
/* Original slow head travel, not AMB code or high-rate wow. 0..100 depth.
 * Deterministic targets every20..60s of processed audio, eight-second easing.
 * Zero depth preserves the r18 echo; switching off slews home, not tape reset. */
int bb_echo_motion_set(bb_echo *, unsigned depth);
/* Mono send input; stereo wet-only returns. Call on silence after Stop so
 * repeats can decay. Buffers must be distinct and not alias engine state. */
int bb_echo_process(bb_echo *, const float *send, float *left, float *right,
                    unsigned frames);
#endif
