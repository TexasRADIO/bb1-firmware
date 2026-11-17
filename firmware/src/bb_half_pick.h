                                        
                                                                                                 
                                                                                                       
                                                                                                                  
                                                                                                                 
                                                                                                                
                                                                     
#pragma once
#include "bb_nav.h"
#include "bb_p1_measure.h"
                                                                                                                 
                                                                                                                  
                                                                                                                     
                                                                                                                      
                                                                               
#define BB_HALF_LIT_RELEASE 3u
/* the mid level of the half-lit (glowing) LEDs at a setting: 3 and 4 light them 1 frame in N */
static inline uint8_t bb_half_mid(unsigned setting, uint8_t dim) { return setting >= 3u ? dim : (uint8_t)0u; }
/* the glow at a setting (the measurement picker): 1 = DIM LO; setting 0 is MENU > LEDS */
static inline unsigned bb_half_glow_lo(unsigned setting, int menu_lo) { return setting ? setting == 2u || setting == 4u : menu_lo != 0; }
#if BB_P1_HALF_LIT_PICKER
#define BB_HALF_MID(dim) bb_half_mid(bb_half_pick, (dim))   /* measurement builds: the picker's setting */
#else
#define BB_HALF_MID(dim) bb_half_mid(BB_HALF_LIT_RELEASE, (dim))
#endif
/* help up or HOME held before the scan: the firmware wrapper and the host test use this same expression */
#define BB_HALF_PICK_QUIET_BEFORE(s) ((s)->help || (s)->home_down)
static inline int bb_half_pick_quiet(const bb_nav_state *s, uint32_t held_logical)
{
    return s->help || s->home_down || (held_logical & BB_NAV_BIT(BB_NAV_B_HOME)) != 0;
}
static inline int bb_half_pick_step(const bb_nav_state *after, int quiet_before, uint32_t pressed_logical,
                                    uint32_t held_logical, unsigned current)
{
    const uint32_t oct = BB_NAV_BIT(BB_NAV_B_OCTDN) | BB_NAV_BIT(BB_NAV_B_OCTUP);
    if (quiet_before || bb_half_pick_quiet(after, held_logical)) return -1;
    if (after->page != BB_NAV_GLOBAL || after->bank || !(pressed_logical & oct)) return -1;
    return (int)((current + ((pressed_logical & BB_NAV_BIT(BB_NAV_B_OCTUP)) ? 1u : 4u)) % 5u);
}
