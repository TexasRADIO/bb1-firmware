/* SPDX-License-Identifier: GPL-3.0-only */
#include "compact_scene.h"
#include "bb_session.h"
#include "bb_wire.h"
#include <string.h>
int compact_capture(compact_scene *out,const bb_core *c)
{
    unsigned m,n;
    if(!out || !c || !bb_wire_separated(out,sizeof *out,c,sizeof *c) || !bb_session_state_valid(c))return 0;
    memset(out,0,sizeof *out);
    memcpy(out->drums,c->drums,sizeof out->drums);
    for(m=0;m<BB_MACHINES;m++) {out->drums[m].next=0;out->drums[m].sound_revision=1;}
    for(m=0;m<BB_SYNTHS;m++) {
        out->synths[m].length=c->synths[m].length;
        out->synths[m].muted=c->synths[m].muted;
        for(n=0;n<BB_STEPS;n++) {
            const bb_note *note=&c->synths[m].notes[n];
            out->synths[m].notes[n]=(uint16_t)(note->note | (note->gate<<7) |
                                                (note->accent<<8) | (note->slide<<9));
        }
    }
    out->dillo=c->dillo;out->telegram=c->telegram;out->effects=c->effects;
    memcpy(out->band_level,c->band_level,sizeof out->band_level);
    out->bpm=c->bpm;out->swing=c->swing;
    return 1;
}
int compact_prepare(bb_core *c,const compact_scene *in,unsigned rate)
{
    unsigned m,n;
    if(!c || !in || !bb_wire_separated(c,sizeof *c,in,sizeof *in) || !bb_init(c,rate))return 0;
    memcpy(c->drums,in->drums,sizeof c->drums);
    for(m=0;m<BB_MACHINES;m++){c->drums[m].next=0;c->drums[m].sound_revision=1;}
    for(m=0;m<BB_SYNTHS;m++) {
        c->synths[m].length=in->synths[m].length;c->synths[m].muted=in->synths[m].muted;
        for(n=0;n<BB_STEPS;n++) {
            unsigned packed=in->synths[m].notes[n];
            bb_note *note=&c->synths[m].notes[n];
            if(packed&~1023u)return 0;
            note->note=(uint8_t)(packed&127u);note->gate=(uint8_t)((packed>>7)&1u);
            note->accent=(uint8_t)((packed>>8)&1u);note->slide=(uint8_t)((packed>>9)&1u);
        }
    }
    c->dillo=in->dillo;c->telegram=in->telegram;c->effects=in->effects;
    memcpy(c->band_level,in->band_level,sizeof c->band_level);
    c->bpm=in->bpm;c->swing=c->active_swing=in->swing;
    return bb_session_state_valid(c);
}
