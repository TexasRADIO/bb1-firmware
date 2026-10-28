/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
typedef struct { unsigned passed,cases; uint32_t ticks[12]; } bb_dense_sweep_result;
int bb_dense_peak_sweep(bb_dense_sweep_result *,uint32_t (*clock_read)(void));
