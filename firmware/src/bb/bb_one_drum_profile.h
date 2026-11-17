/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ONE_DRUM_STUDIO_PROFILE_H
#define BB_ONE_DRUM_STUDIO_PROFILE_H
/* Dillo Duo (BB_WITH_TELEGRAM=1): the Telegram mono voice is Dillo's second
 * voice, the LEAD (synth 1, MIDI channel 2), inside the DILLO instrument.
 * It is not a separate SELECT slot. Without Duo the lead must stay muted. */
#if BB_ONE_DRUM_STUDIO != 1 || BB_OWNER_AUDIO_BRIDGE || BB_WITH_DILLO != 1 || (BB_WITH_TELEGRAM != 0 && BB_WITH_TELEGRAM != 1) || BB_WITH_909_ROUTE || BB_WITH_PARTS
#error "One-drum studio requires Dillo (optionally with its Duo lead), CPU0 ownership, no 909 route or combined bank loader"
#endif
static int bb_one_drum_core_ok(const bb_core *c) {
    if(!c)return 0;
#if !BB_WITH_TELEGRAM
    if(!c->synths[1].muted)return 0;
#endif
    /* A malformed restored scene or corrupted live state must never reach
     * tick(): it indexes the current step and takes modulo by length. Keep
     * this check at the audio boundary, including muted/disabled players,
     * because their playheads still advance on the shared clock. */
    for(unsigned k=0;k<BB_MACHINES;k++)
        if(!c->drums[k].length || c->drums[k].length>BB_STEPS ||
           c->drums[k].next>=c->drums[k].length)return 0;
    for(unsigned k=0;k<BB_SYNTHS;k++)
        if(!c->synths[k].length || c->synths[k].length>BB_STEPS ||
           c->synths[k].next>=c->synths[k].length)return 0;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
#if defined(BB_TWO_DRUM_TARGET_CANDIDATE)
#if !defined(BB_GROUP_TARGET_CANDIDATE) || !defined(BB_TWO_DRUM_QUALIFY)
#error "Two-drum target requires explicit qualification profile"
#endif
#elif !defined(BB_GROUP_HOST_SERIAL_TEST) || defined(BB_GROUP_TARGET_CANDIDATE)
#error "Two-drum experiment is host-only pending target budget qualification"
#endif
    for(unsigned k=2;k<BB_MACHINES;k++)if(c->drums[k].enabled)return 0;
#else
    for(unsigned k=1;k<BB_MACHINES;k++)if(c->drums[k].enabled)return 0;
#endif
    return 1; /* Synth mute, patches, notes and expression remain independent. */
}
#endif
