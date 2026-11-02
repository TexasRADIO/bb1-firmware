/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_GEN_H
#define BB_GEN_H
#include "bb_evolve.h"
#include "bb_phrase_controls.h"
typedef struct {
 uint64_t gates,accents,locked;
 uint32_t seed;
 uint8_t random,euclid,density,accent,slide;
} bb_gen_dillo_args;
typedef struct {bb_phrase home;uint32_t random,passes;uint8_t ready;} bb_gen_rich;
/* Pure, bounded control-thread transforms. Never import catalogue pointers or
 * mutate the audio owner. Out may equal home/current; failures commit nothing. */
uint64_t bb_gen_bjorklund(uint8_t steps,uint8_t pulses);
uint64_t bb_gen_euclid(unsigned hits,unsigned steps,unsigned rotate,unsigned length);
uint32_t bb_gen_seed(uint32_t seed,unsigned part);
uint32_t bb_gen_phrase_voices(const bb_phrase *);
uint64_t bb_gen_lane_hits(const bb_phrase *,unsigned voice,uint64_t *accent);
int bb_gen_lane_write(const bb_phrase *,bb_phrase *,unsigned voice,uint64_t hits,uint64_t accent,unsigned replace_accent,uint64_t keep);
int bb_gen_phrase_random(const bb_phrase *,bb_phrase *,unsigned density,unsigned accent,unsigned anchor,uint32_t seed,uint64_t locked);
int bb_gen_dillo(const bb_note [BB_STEPS],unsigned,bb_note [BB_STEPS],const bb_evolve_settings *,const bb_gen_dillo_args *);
unsigned bb_gen_changed_steps(const bb_phrase *,const bb_phrase *);
void bb_gen_rich_capture(bb_gen_rich *,const bb_phrase *,uint32_t seed,unsigned part);
int bb_gen_rich_pass(bb_gen_rich *,const bb_phrase *,bb_phrase *,const bb_evolve_settings *,unsigned part);
#endif
