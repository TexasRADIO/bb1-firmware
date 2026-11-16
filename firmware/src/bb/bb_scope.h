#ifndef BB_SCOPE_H
#define BB_SCOPE_H
#include <stdint.h>
/* Audio-owned display telemetry. 64 min/max columns, eight consecutive
 * samples per column: 512 samples / 11.61 ms at 44.1 kHz. Not a spectrum,
 * calibrated meter or triggered laboratory scope. Every sample contributes.
 * Source/snapshot access must be serialized with audio on the target. */
#define BB_SELECTED_SCOPE 1
#define BB_SCOPE_COLUMNS 64u
enum { BB_SCOPE_OFF, BB_SCOPE_KIT_A, BB_SCOPE_KIT_B, BB_SCOPE_KIT_C,
       BB_SCOPE_KIT_D, BB_SCOPE_DILLO, BB_SCOPE_TELEGRAM };
typedef struct { int16_t low,high; } bb_scope_column;
typedef struct {
    unsigned source,bin,phase,valid;
    int16_t low,high;
    uint32_t serial;
    bb_scope_column work[BB_SCOPE_COLUMNS],published[BB_SCOPE_COLUMNS];
    float samples[128];
} bb_scope;
static inline void bb_scope_select(bb_scope *s,unsigned source) {
    if(source>BB_SCOPE_TELEGRAM)source=BB_SCOPE_OFF;
    if(s->source==source)return;
    s->source=source;s->bin=s->phase=s->valid=0;
    /* Old data becomes invalid immediately; do not clear a kilobyte with
     * interrupts masked. Every bin is overwritten before publication. */
}
static inline void bb_scope_capture(bb_scope *s,unsigned frames) {
    if(!s->source || frames>128u)return;
    for(unsigned i=0;i<frames;i++) {
        float x=s->samples[i];
        /* NaN and infinities are bounded for graphics, never fed into DSP. */
        int16_t q=x!=x?0:x>=1.0f?32767:x<=-1.0f?-32767:(int16_t)(x*32767.0f);
        if(!s->phase)s->low=s->high=q;
        else {if(q<s->low)s->low=q;if(q>s->high)s->high=q;}
        if(++s->phase==8u) {
            s->phase=0;s->work[s->bin++]=(bb_scope_column){s->low,s->high};
            if(s->bin==BB_SCOPE_COLUMNS) {
                for(unsigned column=0;column<BB_SCOPE_COLUMNS;column++)
                    s->published[column]=s->work[column];
                s->bin=0;s->valid=1;s->serial++;
            }
        }
    }
}
#endif
