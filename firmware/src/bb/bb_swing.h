/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SWING_H
#define BB_SWING_H
#include <stdint.h>
/* Internal byte retains legacy whole percentages; bit7 adds one half percent.
 * Disk v1 remains whole percent; fractional saves explicitly use disk v2. */
static inline unsigned bb_swing_x2(unsigned value) {return 2u*(value&127u)+(value>>7);}
static inline int bb_swing_valid(unsigned value) {
    return value<=255u && (value&127u)>=50u && bb_swing_x2(value)<=150u;
}
static inline uint8_t bb_swing_encode(unsigned x2) {return (uint8_t)((x2/2u)|((x2&1u)<<7));}
static inline uint32_t bb_swing_denominator(uint32_t rate,unsigned value,unsigned odd) {
    unsigned x2=bb_swing_x2(value);return rate*60u*(odd?200u-x2:x2);
}
#endif
