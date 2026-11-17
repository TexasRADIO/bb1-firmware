/* SPDX-License-Identifier: GPL-3.0-only */
                                                                                                              
  
                                                                                                                 
                                                                                                                    
                                                                                                                 
                                                                                                                    
                                                                                                                    
                                                                                      
  
                                                                                                                     
                                                                                                                
                                                                                                               
                                                                          
                                                                         
#ifndef BB_CHORD_H
#define BB_CHORD_H
#include <stdint.h>
#define BB_CHORD_MS 72u
typedef struct {
    uint64_t set, tog;   /* the chord so far; hold-and-add: the sections to toggle */
    uint32_t t0;         /* ms of the first press */
    uint32_t down;       /* gesture keys still held (bit = key) */
    uint8_t active, hold;
} bb_chord;
/* Key `key` (bit in the held mask) for section `s` went down at `now`. */
static inline void bb_chord_press(bb_chord *c, unsigned key, unsigned s, uint32_t now)
{
    uint64_t b = (uint64_t)1 << (s & 63u);
    if (!c->active) { c->active = 1; c->hold = 0; c->t0 = now; c->set = b; c->tog = 0; c->down = 1u << key; return; }
    c->down |= 1u << key;
    if (c->hold) c->tog ^= b; else c->set |= b;
}
/* Every scan: `held` = the gesture keys' levels, `cur` = the current combination. Returns the set to commit, 0 = none. */
static inline uint64_t bb_chord_poll(bb_chord *c, uint32_t held, uint32_t now, uint64_t cur)
{
    if (!c->active) return 0;
    c->down &= held;
    if (!c->hold && (uint32_t)(now - c->t0) < BB_CHORD_MS) return 0;   /* the window is open: more may join */
    if (!c->hold && c->down) { c->hold = 1; c->tog = 0; return c->set; }  /* closed with keys held: commit, then toggles */
    if (c->down) return 0;
    c->active = 0;
    if (!c->hold) return c->set;
    return c->tog && (cur ^ c->tog) ? cur ^ c->tog : 0;
}
#endif
