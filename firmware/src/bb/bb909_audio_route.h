/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "group_gain.h"
/* Audio-owner hooks, never UI DSP calls. Layer -1 means no bound 909.
 * Exact event-span lifetime: hit -> begin -> CPU0 synth/other-kit work ->
 * checked view -> mix -> release. View cannot survive the next begin. */
typedef struct {const float *left,*right,*echo,*spring;} bb909_audio_view;
int bb909_audio_layer(void);
int bb909_audio_hit(unsigned layer,unsigned voice,unsigned velocity);
int bb909_audio_begin(unsigned frames,gg_state *groups);
int bb909_audio_finish(unsigned frames,bb909_audio_view *);
int bb909_audio_release(gg_state *groups);

int bb909_audio_control_reset(void);
int bb909_audio_parameter(unsigned layer,unsigned voice,unsigned parameter,unsigned value);
uint32_t bb909_audio_epoch(void);
int bb909_audio_parameter_epoch(unsigned layer,unsigned voice,unsigned parameter,unsigned value,uint32_t epoch);
