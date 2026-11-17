                                        
                                                                                                      
                                                                                                   
                                                                                                            
                                                                                                             
                                                                                                                  
                                                                                                             
                                          
#pragma once
#include <stdint.h>
#define BB_AUD_SETTLE_MS 350u
#define BB_AUD_PARAMS 8u                      /* BB_PARAM_CUTOFF .. BB_PARAM_SLIDE_MS: what SOUND edits */
typedef struct {
    uint32_t since;                          /* last PRESETS detent */
    uint8_t armed, saved, restore;           /* settle-load owed; restore point valid; next restore command 1..9 */
    uint8_t keep;                            /* KEEP owed: 1 to load (or a request in flight), 2 accepted, not yet heard */
    uint8_t index, edited, scene;            /* the restore point's patch and its EDITED / scene marks */
    uint16_t value[BB_AUD_PARAMS];
} bb_audition;
/* A detent on the page. Returns 1 when the restore point must be taken now (first detent of this audition). */
static inline int bb_audition_detent(bb_audition *a, uint32_t now)
{
    a->since = now; a->armed = 1;
    if (a->saved || a->restore) return 0;
    a->saved = 1; return 1;
}
static inline int bb_audition_due(const bb_audition *a, uint32_t now, int on_page, int pending)
{
    return on_page && a->armed && !a->restore && !pending && now - a->since >= BB_AUD_SETTLE_MS;
}
static inline void bb_audition_leave(bb_audition *a) { a->armed = a->saved = a->keep = 0; }   /* any exit but HOME: keep */
static inline void bb_audition_home(bb_audition *a) { if (a->saved) { bb_audition_leave(a); a->restore = 1; } }
                                                                                                                 
                                                                                                                   
                                                                                                              
static inline void bb_audition_keep(bb_audition *a) { a->keep = 1; }
/* One scan of an owed KEEP. pending: a picker request is in flight (wait for it). playing: observed this scan, the
 * owner plays the patch under the cursor (an edit of it counts). load: 1 = accepted, 0 = refused (still owed).
 * Returns 1 when KEEP has completed: the audition ends and the caller says SOUND KEPT, never before. */
typedef int (*bb_audition_load)(void);
static inline int bb_audition_keep_step(bb_audition *a, int pending, int playing, bb_audition_load load)
{
    if (!a->keep || pending) return 0;
    if (playing) { bb_audition_leave(a); return 1; }
    a->armed = 1; a->keep = 1;                                       /* not what plays (never sent, refused, dropped) */
    if (load()) a->keep = 2;                                         /* accepted: wait for the owner, then look again */
    return 0;
}
typedef unsigned (*bb_audition_get)(unsigned param);
typedef int (*bb_audition_send)(int patch, unsigned value, unsigned param);   /* 1 = accepted */
/* Returns 1 when the restore has completed, 0 when nothing is restoring or a push was refused (call again). */
static inline int bb_audition_restore(bb_audition *a, bb_audition_get get, bb_audition_send send)
{
    if (!a->restore) return 0;
    for (; a->restore <= BB_AUD_PARAMS + 1u; a->restore++) {
        unsigned p = a->restore - 2u;
        if (a->restore == 1u ? !send(1, a->index, 0) : get(p) != a->value[p] && !send(0, a->value[p], p)) return 0;
    }
    a->restore = 0; return 1;
}
