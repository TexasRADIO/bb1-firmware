#ifndef BB_ATTRIBUTION_H
#define BB_ATTRIBUTION_H
#include <stdint.h>
/* Diagnostic-only disjoint wall-clock scopes. Independent of the existing
 * six-stage report: never reinterpret its version or frame invariants.
 * One audio writer; snapshot only after the callback completes. Clock probes
 * have observer cost. These ticks are not exclusive CPU utilization. */
enum { BB_ATTR_PREPARE, BB_ATTR_OTHER_KIT, BB_ATTR_ADVANCE,
       BB_ATTR_OUTPUT_CONVERT, BB_ATTR_COPY, BB_ATTR_COUNT };
typedef struct { uint32_t ticks, calls, frames; } bb_attr_cost;
typedef struct {
    bb_attr_cost cost[BB_ATTR_COUNT];
    uint32_t start, errors;
    unsigned active;
} bb_attribution;
static inline void bb_attr_reset(bb_attribution *p) {
    *p=(bb_attribution){0}; p->active=BB_ATTR_COUNT;
}
static inline int bb_attr_begin(bb_attribution *p,unsigned scope,uint32_t now) {
    if(scope>=BB_ATTR_COUNT || p->active!=BB_ATTR_COUNT) {p->errors++;return 0;}
    p->active=scope;p->start=now;return 1;
}
static inline int bb_attr_end(bb_attribution *p,unsigned scope,uint32_t now,uint32_t frames) {
    if(scope>=BB_ATTR_COUNT || p->active!=scope) {p->errors++;return 0;}
    p->cost[scope].ticks+=now-p->start;
    p->cost[scope].calls++;p->cost[scope].frames+=frames;
    p->active=BB_ATTR_COUNT;return 1;
}
static inline int bb_attr_complete(const bb_attribution *p) {
    return !p->errors && p->active==BB_ATTR_COUNT;
}
#endif
