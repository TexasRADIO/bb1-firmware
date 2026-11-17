/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_trio.h"
#include "bb_session.h"
int bb_trio_pack_note(bb_note n,uint16_t *out) {
    if(!out || n.note>127 || n.gate>1 || n.accent>1 || n.slide>1)return 0;
    *out=(uint16_t)(n.note|(n.gate<<7)|(n.accent<<8)|(n.slide<<9));return 1;
}
bb_note bb_trio_unpack_note(uint16_t n) {
    bb_note out={(uint8_t)(n&127u),(uint8_t)((n>>7)&1u),(uint8_t)((n>>8)&1u),(uint8_t)((n>>9)&1u)};
    return out;
}
void bb_trio_unpack_notes(bb_note *dst,const uint16_t *src,unsigned count) {
    for(unsigned n=0;n<count;n++)dst[n]=bb_trio_unpack_note(src[n]);
}
int bb_trio_valid(const bb_trio_program *p) {
    const uint16_t voices=(1u<<BB_DRUM_TRACKS)-1u;
    if(!p || p->primary>=BB_MACHINES || p->initial>=BB_TRIO_PARTS || !bb_session_state_valid(&p->seed))return 0;
    for(unsigned k=0;k<BB_MACHINES;k++)for(unsigned n=0;n<BB_STEPS;n++)
        if((p->seed.drums[k].steps[n].hits&~voices) ||
           (p->seed.drums[k].steps[n].accents&~p->seed.drums[k].steps[n].hits))return 0;
    for(unsigned slot=0;slot<BB_TRIO_PARTS;slot++) {
        const bb_trio_part *s=&p->part[slot];
        if(!s->drum_length || s->drum_length>BB_STEPS ||
           s->phrase<16 || s->phrase>BB_STEPS || s->phrase%16 ||
           (s->mode_mask&~voices) || (s->mode_value&~s->mode_mask))return 0;
        for(unsigned n=0;n<BB_STEPS;n++)
            if((s->drums[n].hits&~voices) || (s->drums[n].accents&~s->drums[n].hits))return 0;
        for(unsigned synth=0;synth<BB_SYNTHS;synth++) {
            if(!s->synth_length[synth] || s->synth_length[synth]>BB_STEPS)return 0;
            for(unsigned n=0;n<BB_STEPS;n++)if(s->notes[synth][n]&~UINT16_C(1023))return 0;
        }
    }
    return 1;
}
uint32_t bb_trio_check(const bb_trio_program *p) {
    uint32_t h=UINT32_C(2166136261);const unsigned char *bytes=(const unsigned char *)p;
    for(unsigned n=0;n<sizeof *p;n++)h=(h^bytes[n])*UINT32_C(16777619);
    return h; /* corruption detection, not authentication or all-fault proof */
}
