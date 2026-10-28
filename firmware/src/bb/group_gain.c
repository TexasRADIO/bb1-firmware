#include "group_gain.h"

void gg_init(gg_state *state)
{
    unsigned i;
    for (i = 0; i < GG_COUNT; ++i) {
        gg_fader *f = &state->fader[i];
        f->current = f->target = 1.0f;
        f->increment = 0.0f;
        f->remaining = 0;
        f->level = 127;
        f->muted = 0;
    }
}

int gg_set(gg_state *state, unsigned group, unsigned level, unsigned muted)
{
    gg_fader *f;
    float target;
    if (group >= GG_COUNT || level > 127 || muted > 1) return 0;
    f = &state->fader[group];
    target = muted ? 0.0f : (float)level / 127.0f;
    f->level = (uint8_t)level;
    f->muted = (uint8_t)muted;
    /* Repeated UI messages must not keep restarting a ramp. */
    if (target == f->target) return 1;
    f->target = target;
    f->remaining = GG_RAMP_FRAMES;
    f->increment = (target - f->current) / (float)GG_RAMP_FRAMES;
    return 1;
}

void gg_frame(gg_state *state, float gains[GG_COUNT])
{
    unsigned i;
    for (i = 0; i < GG_COUNT; ++i) {
        gg_fader *f = &state->fader[i];
        if (f->remaining) {
            --f->remaining;
            if (!f->remaining) f->current = f->target;
            else f->current += f->increment;
        }
        gains[i] = f->current;
    }
}

void gg_advance(gg_state *state, unsigned frames)
{
    unsigned i;
    for (i = 0; i < GG_COUNT; ++i) {
        gg_fader *f = &state->fader[i];
        unsigned n = frames < f->remaining ? frames : f->remaining;
        while (n--) {
            --f->remaining;
            if (!f->remaining) f->current = f->target;
            else f->current += f->increment;
        }
    }
}

float gg_voice(float contribution, float gain)
{
    /* Preserve the original contribution at unity, including signed zero. */
    return gain == 1.0f ? contribution : contribution * gain;
}

int gg_808_sound(unsigned sound)
{
    static const uint8_t groups[16] = {
        GG_KICK, GG_SNARE_CLAP,
        GG_PERCUSSION, GG_PERCUSSION, GG_PERCUSSION,
        GG_PERCUSSION, GG_PERCUSSION, GG_PERCUSSION,
        GG_PERCUSSION, GG_PERCUSSION, GG_PERCUSSION,
        GG_SNARE_CLAP, GG_PERCUSSION, GG_HATS, GG_HATS, GG_PERCUSSION
    };
    return sound < 16 ? groups[sound] : -1;
}
