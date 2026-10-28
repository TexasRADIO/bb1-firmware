#include "bb_wire.h"
/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_session.h"
#include "bb_crc32.h"
#include <string.h>

#if BB_MACHINES != 4 || BB_STEPS != 64 || BB_SYNTHS != 2 || BB_DRUM_SOUNDS != 16 || BB_DRUM_POTS != 9 || BB_DRUM_TRACKS != 11 || BB_DRUM_SOUND_SCHEMA != 1
#error "Session v1 layout requires an explicit migration for changed dimensions"
#endif
typedef char bb_session_fx_layout[(BB_FX_PARAMETER_COUNT == 15) ? 1 : -1];
typedef char bb_session_mix_layout[(BB_MIX_CHANNELS == 3) ? 1 : -1];
typedef char bb_session_wire_layout[(BB_SESSION_BYTES == 16+6+4*(5+64*4+16*9+11+3)+2*(2+64*4)+12+18+15*2) ? 1 : -1];

#define crc_add bb_crc32_add
static uint32_t checksum(const uint8_t *p) {
    return ~crc_add(crc_add(UINT32_MAX,p,12),p+16,BB_SESSION_BYTES-16);
}
static void put16(uint8_t **p, unsigned v) {
    *(*p)++=(uint8_t)v; *(*p)++=(uint8_t)(v>>8);
}
static unsigned get16(const uint8_t **p) {
    unsigned v=(*p)[0]|((unsigned)(*p)[1]<<8); *p+=2; return v;
}
#define put32 bb_wire_put32
#define get32 bb_wire_get32
int bb_session_state_valid(const bb_core *c) {
    unsigned m,i,j,lo,hi;
    if(!c)return 0;
    const bb_dillo_patch *d=&c->dillo;
    if(c->bpm<30 || c->bpm>300 || !bb_swing_valid(c->swing))return 0;
    for(i=0;i<BB_MIX_CHANNELS;i++)if(c->band_level[i]>127)return 0;
    for(m=0;m<BB_MACHINES;m++) {
        const bb_machine *a=&c->drums[m];const bb_drum_sound *s=&a->sound;
        if(!a->length || a->length>64 || a->kit>=BB_DRUM_KITS || a->enabled>1 || a->muted>1 || a->level>127)return 0;
        if(s->kit_level>127 || s->kit_accent>127 || s->edited>1)return 0;
        for(i=0;i<BB_DRUM_TRACKS;i++)if(s->mode[i]>1)return 0;
        for(i=0;i<BB_DRUM_SOUNDS;i++)for(j=0;j<BB_DRUM_POTS;j++)
            if(s->pot[i][j]>(j==4?6:127))return 0;
    }
    for(m=0;m<BB_SYNTHS;m++) {
        const bb_sequence *s=&c->synths[m];
        if(!s->length || s->length>64 || s->muted>1)return 0;
        for(i=0;i<64;i++) {
            const bb_note *n=&s->notes[i];
            if(n->note>127 || n->gate>1 || n->accent>1 || n->slide>1)return 0;
        }
    }
    /* Match the production Dillo adapter's accepted recipe domain. */
    if(d->cutoff_hz<20 || d->cutoff_hz>16000 || d->resonance_x100>2000 ||
       d->wave>1 || d->envelope>100 || d->decay>100 || d->accent>100 ||
       d->drive>100 || !d->slide_ms || !d->attack_ms || d->tracking>100)return 0;
    if(!bb_telegram_patch_valid(&c->telegram))return 0;
    for(i=0;i<BB_FX_PARAMETER_COUNT;i++)
        if(!bb_fx_parameter_limits(i,&lo,&hi) || c->effects.value[i]<lo || c->effects.value[i]>hi)return 0;
    return 1;
}
int bb_session_encode(const bb_core *c,uint8_t *dst,size_t capacity) {
    unsigned m,i,j;uint8_t *p;
    const bb_dillo_patch *d;const bb_telegram_patch *t;
    if(!c || !dst || capacity<BB_SESSION_BYTES || !bb_session_state_valid(c))return 0;
    p=dst;memcpy(p,"BBSC",4);p+=4;put16(&p,(c->swing&128u)?2u:BB_SESSION_VERSION);put16(&p,16);
    put32(p,BB_SESSION_BYTES);p+=8;
    put16(&p,c->bpm);*p++=(c->swing&128u)?(uint8_t)bb_swing_x2(c->swing):c->swing;
    for(i=0;i<3;i++)*p++=c->band_level[i];
    for(m=0;m<4;m++) {
        const bb_machine *a=&c->drums[m];const bb_drum_sound *s=&a->sound;
        *p++=a->length;*p++=a->kit;*p++=a->enabled;*p++=a->muted;*p++=a->level;
        for(i=0;i<64;i++){put16(&p,a->steps[i].hits);put16(&p,a->steps[i].accents);}
        for(i=0;i<16;i++)for(j=0;j<9;j++)*p++=s->pot[i][j];
        for(i=0;i<11;i++)*p++=s->mode[i];
        *p++=s->kit_level;*p++=s->kit_accent;*p++=s->edited;
    }
    for(m=0;m<2;m++) {
        const bb_sequence *s=&c->synths[m];
        *p++=s->length;*p++=s->muted;
        for(i=0;i<64;i++) {
            const bb_note *n=&s->notes[i];
            *p++=n->note;*p++=n->gate;*p++=n->accent;*p++=n->slide;
        }
    }
    d=&c->dillo;put16(&p,d->cutoff_hz);put16(&p,d->resonance_x100);
    *p++=d->wave;*p++=d->envelope;*p++=d->decay;*p++=d->accent;
    *p++=d->drive;*p++=d->slide_ms;*p++=d->attack_ms;*p++=d->tracking;
    t=&c->telegram;put16(&p,t->cutoff_hz);put16(&p,t->resonance_x100);
    *p++=t->square;*p++=t->saw;*p++=t->sub;*p++=t->envelope;*p++=t->modulation;*p++=t->glide;
    put16(&p,t->lfo_rate_x100);put16(&p,t->attack_ms);put16(&p,t->release_ms);put16(&p,t->glide_ms);
    for(i=0;i<15;i++)put16(&p,c->effects.value[i]);
    put32(dst+12,checksum(dst));return 1;
}
int bb_session_decode(const uint8_t *src,size_t length,uint32_t rate,bb_core *out,bb_core *c) {
    const uint8_t *p;unsigned m,i,j,version;bb_dillo_patch *d;bb_telegram_patch *t;
    if(!src || !out || !c || out==c || length!=BB_SESSION_BYTES)return 0;
    if(memcmp(src,"BBSC",4) || get32(src+8)!=BB_SESSION_BYTES)return 0;
    p=src+4;version=get16(&p);if((version!=1u && version!=2u) || get16(&p)!=16 || get32(src+12)!=checksum(src))return 0;
    if(!bb_init(c,rate))return 0;
    p=src+16;c->bpm=(uint16_t)get16(&p);c->swing=*p++;
    if(version==1u) {if(c->swing<50 || c->swing>75)return 0;}
    else {
        if(c->swing<101 || c->swing>149 || !(c->swing&1u))return 0;
        c->swing=bb_swing_encode(c->swing);
    }

    for(i=0;i<3;i++)c->band_level[i]=*p++;
    for(m=0;m<4;m++) {
        bb_machine *a=&c->drums[m];bb_drum_sound *s=&a->sound;
        a->length=*p++;a->kit=*p++;a->enabled=*p++;a->muted=*p++;a->level=*p++;
        for(i=0;i<64;i++){a->steps[i].hits=(uint16_t)get16(&p);a->steps[i].accents=(uint16_t)get16(&p);}
        for(i=0;i<16;i++)for(j=0;j<9;j++)s->pot[i][j]=*p++;
        for(i=0;i<11;i++)s->mode[i]=*p++;
        s->kit_level=*p++;s->kit_accent=*p++;s->edited=*p++;
    }
    for(m=0;m<2;m++) {
        bb_sequence *s=&c->synths[m];s->length=*p++;s->muted=*p++;
        for(i=0;i<64;i++) {
            bb_note *n=&s->notes[i];n->note=*p++;n->gate=*p++;n->accent=*p++;n->slide=*p++;
        }
    }
    d=&c->dillo;d->cutoff_hz=(uint16_t)get16(&p);d->resonance_x100=(uint16_t)get16(&p);
    d->wave=*p++;d->envelope=*p++;d->decay=*p++;d->accent=*p++;
    d->drive=*p++;d->slide_ms=*p++;d->attack_ms=*p++;d->tracking=*p++;
    t=&c->telegram;t->cutoff_hz=(uint16_t)get16(&p);t->resonance_x100=(uint16_t)get16(&p);
    t->square=*p++;t->saw=*p++;t->sub=*p++;t->envelope=*p++;t->modulation=*p++;t->glide=*p++;
    t->lfo_rate_x100=(uint16_t)get16(&p);t->attack_ms=(uint16_t)get16(&p);
    t->release_ms=(uint16_t)get16(&p);t->glide_ms=(uint16_t)get16(&p);
    for(i=0;i<15;i++)c->effects.value[i]=(uint16_t)get16(&p);
    if((size_t)(p-src)!=BB_SESSION_BYTES || !bb_session_state_valid(c))return 0;
    c->active_swing=c->swing;*out=*c;return 1;
}
