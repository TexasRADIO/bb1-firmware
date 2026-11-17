/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SINGLE_PROFILE_H
#define BB_SINGLE_PROFILE_H
#if BB_SINGLE_DRUM_ONLY != 1 || BB_OWNER_AUDIO_BRIDGE || BB_WITH_DILLO || BB_WITH_TELEGRAM || BB_WITH_909_ROUTE || BB_WITH_PARTS
#error "Drum-only profile must compile without synths, owner bridge, 909 route or combined arrangement loader"
#endif
static int bb_single_core_ok(const bb_core *c) {
    if(!c)return 0;
    for(unsigned k=1;k<BB_MACHINES;k++)if(c->drums[k].enabled)return 0;
    for(unsigned k=0;k<BB_SYNTHS;k++)if(!c->synths[k].muted)return 0;
    return 1;
}
#endif
