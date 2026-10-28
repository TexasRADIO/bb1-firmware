/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from Felucca's spring algorithm, copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hügelton Instruments.
 * Float adaptation by Big Beat; see EFFECTS_PROVENANCE.md. */
#ifndef BB_SPRING_H
#define BB_SPRING_H
#include <stdint.h>
#define BB_SPRING_RING 4096u
#define BB_SPRING_STAGES 10u
/* A dispersive spring candidate, not a generic room or a calibrated tank.
 * Mono wet return. No heap. Length changes slew per sample, not per block.
 * The reference fixed-point implementation remains unchanged upstream. */
typedef struct {
    float line[BB_SPRING_RING], allpass[4u*(BB_SPRING_STAGES+1u)];
    float length, target_length, low, high, damping, feedback, wet;
    uint32_t sample_rate, write, invalid_frames;
    uint32_t pristine; /* Set only by init; cleared before processing any nonzero input. */
} bb_spring;
/* This full-rate line supports 30..60 ms across 8..48 kHz. */
int bb_spring_init(bb_spring *,unsigned sample_rate);
int bb_spring_set(bb_spring *,unsigned size,unsigned damping,unsigned wet);
/* Separate input/output buffers, 1..128 frames; silence continues the tail.
 * Buffers must not overlap each other or engine state. */
int bb_spring_process(bb_spring *,const float *send,float *out,unsigned frames);
#endif
