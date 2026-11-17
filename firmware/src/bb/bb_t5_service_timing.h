/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_T5_SERVICE_TIMING_H
#define BB_T5_SERVICE_TIMING_H
#include <stdint.h>
enum {BB_T5_SCAN_PREFIX,BB_T5_USB,BB_T5_UART,BB_T5_UAC,BB_T5_OUTSIDE,BB_T5_SERVICE_KINDS};
typedef struct {
    uint32_t calls[BB_T5_SERVICE_KINDS],last_ticks[BB_T5_SERVICE_KINDS];
    uint32_t max_ticks[BB_T5_SERVICE_KINDS],max_at_ms[BB_T5_SERVICE_KINDS];
} bb_t5_service_stats;
/* One TIMER5 writer. Main-loop readers use the existing IRQ-guarded snapshot.
 * No reset command; counts wrap unsigned and maxima retain boot history. */
static volatile bb_t5_service_stats bb_t5_services;
static inline void bb_t5_record(unsigned kind,uint32_t elapsed,uint32_t now_ms){
    if(kind>=BB_T5_SERVICE_KINDS)return;
    bb_t5_services.calls[kind]++;
    bb_t5_services.last_ticks[kind]=elapsed;
    if(elapsed>bb_t5_services.max_ticks[kind]){
        bb_t5_services.max_ticks[kind]=elapsed;
        bb_t5_services.max_at_ms[kind]=now_ms;
    }
}
#endif
