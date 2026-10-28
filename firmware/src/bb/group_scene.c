/* SPDX-License-Identifier: GPL-3.0-only */
#include "group_scene.h"
#include "bb_crc32.h"
#include <string.h>
_Static_assert(BB_MACHINES==4 && GG_COUNT==4,"BBGM v1 kit and group dimensions");
static int unity(const uint8_t *p){
    for(unsigned i=0;i<GG_SCENE_MIX_BYTES;i++)if(p[i]!=127)return 0;
    return 1;
}
static uint32_t checksum(const uint8_t *p,size_t n){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,12),p+16,n-16);
}
int gg_scene_capture(const gg_state bank[4],uint8_t packed[GG_SCENE_MIX_BYTES]){
    if(!bb_wire_separated(bank,4*sizeof *bank,packed,GG_SCENE_MIX_BYTES))return 0;
    for(unsigned k=0;k<4;k++)for(unsigned g=0;g<GG_COUNT;g++)
        if(bank[k].fader[g].level>127||bank[k].fader[g].muted>1)return 0;
    for(unsigned k=0;k<4;k++)for(unsigned g=0;g<GG_COUNT;g++)
        packed[k*GG_COUNT+g]=(uint8_t)(bank[k].fader[g].level|(bank[k].fader[g].muted<<7));
    return 1;
}
static void restore_layer(gg_state *state,const uint8_t *packed){
        memset(state,0,sizeof *state);
        gg_init(state);
        for(unsigned g=0;g<GG_COUNT;g++){
            gg_fader *f=&state->fader[g];unsigned v=packed[g];
            f->level=(uint8_t)(v&127u);f->muted=(uint8_t)(v>>7);
            f->current=f->target=f->muted?0.0f:(float)f->level/127.0f;
        }
}
int gg_scene_restore_layer(gg_state *state,const uint8_t packed[4]){
    if(!bb_wire_separated(state,sizeof *state,packed,4))return 0;
    restore_layer(state,packed);return 1;
}
int gg_scene_restore(gg_state bank[4],const uint8_t packed[GG_SCENE_MIX_BYTES]){
    if(!bb_wire_separated(bank,4*sizeof *bank,packed,GG_SCENE_MIX_BYTES))return 0;
    for(unsigned k=0;k<4;k++)restore_layer(&bank[k],packed+4*k);
    return 1;
}
size_t gg_scene_encode(const bb_family_scene *s,const uint8_t *packed,void *output,size_t capacity){
    if(!s||!packed||!output||!bb_family_scene_valid(s))return 0;
    unsigned family=0;for(unsigned k=0;k<4;k++)family|=s->family[k];
    size_t base=family?BB_FAMILY_SCENE_BYTES:BB_SESSION_BYTES;
    int extended=!unity(packed);size_t bytes=base+(extended?GG_SCENE_OVERHEAD:0);
    if(capacity<bytes||!bb_wire_separated(output,bytes,s,sizeof *s)||
       !bb_wire_separated(output,bytes,packed,GG_SCENE_MIX_BYTES))return 0;
    uint8_t *wire=output;
    if(!extended)return bb_family_scene_encode(s,wire,capacity);
    /* All preconditions for the nested encoder were checked before writing. */
    if(bb_family_scene_encode(s,wire+GG_SCENE_OVERHEAD,base)!=base)return 0;
    memcpy(wire,"BBGM",4);wire[4]=1;wire[5]=0;wire[6]=GG_SCENE_OVERHEAD;wire[7]=0;
    bb_wire_put32(wire+8,(uint32_t)bytes);memcpy(wire+16,packed,GG_SCENE_MIX_BYTES);
    bb_wire_put32(wire+12,checksum(wire,bytes));return bytes;
}
int gg_scene_decode_staged(const void *input,size_t bytes,uint32_t rate,
                           bb_family_scene_workspace *work,uint8_t *packed){
    if(!input||!work||!packed||!bb_wire_separated(input,bytes,work,sizeof *work)||
       !bb_wire_separated(input,bytes,packed,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(work,sizeof *work,packed,GG_SCENE_MIX_BYTES))return 0;
    const uint8_t *wire=input;
    if(bytes==BB_SESSION_BYTES||bytes==BB_FAMILY_SCENE_BYTES){
        if(!bb_family_scene_decode_staged(input,bytes,rate,work))return 0;
        memset(packed,127,GG_SCENE_MIX_BYTES);return 1;
    }
    if((bytes!=BB_SESSION_BYTES+GG_SCENE_OVERHEAD&&bytes!=GG_SCENE_BYTES)||
       memcmp(wire,"BBGM",4)||wire[4]!=1||wire[5]||wire[6]!=GG_SCENE_OVERHEAD||wire[7]||
       bb_wire_get32(wire+8)!=bytes||bb_wire_get32(wire+12)!=checksum(wire,bytes)||unity(wire+16))return 0;
    if(!bb_family_scene_decode_staged(wire+GG_SCENE_OVERHEAD,bytes-GG_SCENE_OVERHEAD,rate,work))return 0;
    memcpy(packed,wire+16,GG_SCENE_MIX_BYTES);return 1;
}
