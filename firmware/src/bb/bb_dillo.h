#ifndef BB_DILLO_H
#define BB_DILLO_H
#include "bb_core.h"
#include "bass303.h"
#include "bb_note_expression.h"
#if BASS303_CTRL != 1
#error "Dillo integration currently requires -DBASS303_CTRL=1 in all voice/adapter units; reduced-rate interpolation is caller-block dependent"
#endif
/* Native Dillo articulation around the retained Open303-derived X0X voice.
 * This is a new embedded interpretation, not sample-equivalent to Studio's
 * Web Audio biquad. No claim of a full Devil Fish model or real-bass synthesis. */
typedef struct {
    bass303_t voice;
    bb_dillo_patch sound;
    uint32_t release_in;
    uint8_t slide_out,has_sound,last_note;
    float expression_gain,expression_target,expression_delta;
    uint8_t expression_remaining;
} bb_dillo;
void bb_dillo_init(bb_dillo *);
int bb_dillo_sound(bb_dillo *,const bb_dillo_patch *);
/* slide belongs to the current step and connects INTO the following note.
 * next_gate is a lookahead at that next step, not a request to change its pitch.
 * Ordinary gates use Studio's default 65% step duration. */
void bb_dillo_step(bb_dillo *,bb_note,uint32_t step_frames,int next_gate);
/* Sequenced expression is independent of accent. Present velocity uses the
 * Studio /104 relative amplitude with a fixed64sample de-click ramp; this is
 * an explicit native adaptation, not WebAudio sample equivalence. */
int bb_dillo_step_expression(bb_dillo *,bb_note,uint32_t,int,bb_note_expression);
/* Audio-owner held key: explicit legato, release only through stop/next step.
 * Returns 0 for invalid MIDI note/velocity, without changing the voice.
 * Velocity >=100 selects the native accent, not an invented linear VCA. */
int bb_dillo_key(bb_dillo *,unsigned note,unsigned velocity,int legato);
void bb_dillo_stop(bb_dillo *);
/* Writes mono output, supports arbitrary caller block lengths by bounded chunks. */
void bb_dillo_render(bb_dillo *,float *,uint32_t frames);
#endif
