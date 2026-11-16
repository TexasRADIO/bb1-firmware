/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SAMPLE_FLOOR_Q15_R1_H
#define BB_SAMPLE_FLOOR_Q15_R1_H
#include <stdint.h>
/* Exact floor(x/32768), with defined unsigned shifts for every int32 value.
 * Casting negative x to uint32 adds 2^32. After shifting by15 that adds
 * exactly131072, which is removed in the signed result. No signed shift,
 * division, reciprocal, negative overflow or changed interpolation rounding.
 * The shifted unsigned value fits int32 (0..131071); result -65536..65535. */
static int32_t bb_sample_floor_q15(int32_t x) {
    return (int32_t)((uint32_t)x>>15)-(x<0?131072:0);
}
#endif
