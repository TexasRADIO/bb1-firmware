/* SPDX-License-Identifier: GPL-3.0-only
 * Additional exception context in a separate retained section. The legacy
 * fm1_crash_t layout and address remain unchanged for recovery readers. */
#ifndef FM1_CRASH_EXTRA_H
#define FM1_CRASH_EXTRA_H
#include <stdint.h>
#define FM1_CRASH_EXTRA_MAGIC 0x43525831u /* CRX1 */
typedef struct {
    volatile uint32_t magic;
    uint32_t count;
    uint32_t r[16];
    uint32_t usp,ssp;
} fm1_crash_extra_t;
_Static_assert(sizeof(fm1_crash_extra_t)==80u,"crash context reserve");
static inline void fm1_crash_extra_copy(fm1_crash_extra_t *out,
                                         const uint32_t *frame,uint32_t count) {
    out->magic=0;
    out->count=count;
    for(unsigned i=0;i<16u;i++)out->r[i]=frame[i];
    out->usp=frame[22];out->ssp=frame[23];
    __asm__ volatile("" ::: "memory");
    out->magic=FM1_CRASH_EXTRA_MAGIC;
}
static inline int fm1_crash_extra_matches(const fm1_crash_extra_t *extra,
                                           uint32_t legacy_count) {
    return extra->magic==FM1_CRASH_EXTRA_MAGIC && extra->count==legacy_count;
}
#endif
