                                        
                                                                                                   
                                                                                                                  
                                                                                                                    
                                                                                                              
                                                                                     
  
                                                                                                          
                                                                                                            
#ifndef BB_UI_PLAY_H
#define BB_UI_PLAY_H
#include <stdint.h>

#define BB_PL_PADS 11
enum { BB_PL_FDBK, BB_PL_WET, BB_PL_TONE, BB_PL_DRIVE, BB_PL_WEAR, BB_PL_MOTION, BB_PL_SIZE, BB_PL_DAMP, BB_PL_SPRING_WET };
typedef struct {
    /* what moves frame to frame (each element hashes its own bit of these) */
    uint16_t pad_lit;                   /* pads flashing now: a pattern hit or a finger                  */
    uint32_t keys_sounding;             /* KEYS: keys whose note is sounding (held or the playing step)   */
    /* the rest: one hash for the page (bb_ui_play.c pl_els) */
    const char *pad_name[BB_PL_PADS];   /* the track names of the kit ("BASS DRUM", "CLOSED HAT")       */
    uint8_t pad_group[BB_PL_PADS];      /* the K1-K4 group of each track (KICK SN/CL HATS PERC), 0xFF none */
    uint8_t instr_muted;                /* bits A, B, DILLO (the kill / instrument mutes)                */
    const char *kit_name[2];            /* Big Beat landing: the two machines, named as the kits heard    */
    uint8_t kit_on;                     /* bits A/B: the layer is enabled                                 */
    uint8_t landing;                    /* HOME's Big Beat performance surface, not a deeper drum page    */
    /* Dillo */
    uint8_t keys;                       /* 1: Dillo KEYS (ARP), 0: Dillo STEPS (SEQ)                      */
    uint8_t key_note[27];               /* the MIDI note each key plays (KEYS) or sets (STEPS, black keys) */
    uint8_t step_note[16];              /* STEPS: MIDI note of each visible step (gate in step_gate)      */
    uint16_t step_gate, step_held;      /* steps with a note; the step(s) held under a finger             */
    /* FX: the values the knobs set, 0..255 unless noted */
    uint16_t echo_ms;                   /* ECHO TIME, ms                                                  */
    uint8_t heads;                      /* ECHO HEADS bits 1-3                                            */
    uint8_t fx[9];                      /* BB_PL_FDBK ..: in bb_ui_play_firmware.inc's parameter order     */
    uint8_t send[3][2];                 /* SENDS: KIT A / KIT B / DILLO into ECHO / SPRING                */
    uint8_t focus;                      /* the focused part (0 A, 1 B, 2 DILLO)                           */
} bb_ui_play;

void bb_ui_play_set(const bb_ui_play *p);

#ifndef BB_UI_HOOK_KEY
#define BB_UI_HOOK_KEY(key, bright) ((void)0)   /* host test: a key shown on screen (bright = its lit look) */
#endif
#endif
