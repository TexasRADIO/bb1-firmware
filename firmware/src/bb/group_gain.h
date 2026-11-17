#ifndef BB_PROPOSAL_GROUP_GAIN_H
#define BB_PROPOSAL_GROUP_GAIN_H
#include <stdint.h>

/* Offline proposal, not linked to firmware. Audio thread owns this state. */
enum { GG_KICK, GG_SNARE_CLAP, GG_HATS, GG_PERCUSSION, GG_COUNT };
enum { GG_RAMP_FRAMES = 256 };
typedef struct {
    float current, target, increment;
    unsigned remaining;
    uint8_t level, muted;
} gg_fader;
typedef struct { gg_fader fader[GG_COUNT]; } gg_state;
void gg_init(gg_state *state);
int gg_set(gg_state *state, unsigned group, unsigned level, unsigned muted);
/* Exactly once per audio frame, including frames with no active voices. */
void gg_frame(gg_state *state, float gains[GG_COUNT]);
/* Silent engine: advance the same ramps without materialising unused gains.
 * Keep every floating-point addition for unfinished ramps bit-identical. */
void gg_advance(gg_state *state, unsigned frames);
/* Input is one already shaped/scaled voice contribution, not a mixed bus. */
float gg_voice(float contribution, float gain);
/* Sound identifiers follow the retained drum808.h D8S enum. -1 is invalid. */
int gg_808_sound(unsigned sound);
#endif
