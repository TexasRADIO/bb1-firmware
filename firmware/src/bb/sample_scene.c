/* SPDX-License-Identifier: GPL-3.0-only */
#include "sample_scene.h"
#include "bb_crc32.h"
#include "bb_sample_gain.h"
#include <string.h>
_Static_assert(sizeof(float)==4,"sample scene gain requires binary32");
_Static_assert(BB_SAMPLE_SCENE_BYTES<=3840u,"fits existing project journal");
static uint32_t sample_scene_crc(const uint8_t *p,size_t n){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,12),p+16,n-16);
}
/* 1.2 scene conversion: a scene saved against the previous library is accepted when the installed library
 * declares that library its predecessor (bb_sample_boot does, only when kits 0..pred_kits-1 are the same machines in
 * the same slots). Its kit IDs must lie inside the predecessor's kits; the decoded identity is pinned to the
 * installed library, so the next save is a current scene. The scene's own CRC, sizes, reserved bytes and every
 * other check are unchanged; a scene pinned to any other library is still refused. */
static uint32_t pred_header,pred_payload;
static unsigned pred_kits;
void bb_sample_scene_set_predecessor(uint32_t header,uint32_t payload,unsigned kits){
    pred_header=header;pred_payload=payload;pred_kits=kits<=8?kits:0;
}
/* how many kit IDs a scene pinned to (h,p) may use under the installed library (header,payload,count); 0 = refused */
static unsigned sample_scene_kits(const uint8_t *p,uint32_t header,uint32_t payload,unsigned count){
    uint32_t h=bb_wire_get32(p+16),q=bb_wire_get32(p+20);
    if(h==header&&q==payload)return count;
    if(pred_kits&&h==pred_header&&q==pred_payload)return pred_kits<count?pred_kits:count;
    return 0;
}
static int sample_scene_identity_valid(const bb_sample_scene_identity *i){
    return i && i->kit<8 && i->kind<=1 && (!i->kind||!i->kit) && i->echo<=127 && i->spring<=127 &&
           i->gain>0.0f && i->gain<=1.0f;
}
size_t bb_sample_scene_encode(const bb_family_scene *s,const uint8_t *mix,
                             const bb_sample_scene_identity *id,void *output,size_t capacity){
    if(!s||!mix||!output||!sample_scene_identity_valid(id)||
       !bb_family_scene_valid(s))return 0;
    /* Exactly one hybrid drum owner, not independent909 layers. Samples retain
     * byte-identicalv1 encoding; explicit808 identity usesv2. */
    for(unsigned k=0;k<4;k++)if(s->family[k])return 0;
    int extended=0;for(unsigned k=0;k<16;k++)extended|=mix[k]!=127;
    size_t nested=BB_SESSION_BYTES+(extended?GG_SCENE_OVERHEAD:0);
    size_t bytes=nested+BB_SAMPLE_SCENE_OVERHEAD;
    if(capacity<bytes||!bb_wire_separated(output,bytes,s,sizeof *s)||
       !bb_wire_separated(output,bytes,mix,GG_SCENE_MIX_BYTES)||
       !bb_wire_separated(output,bytes,id,sizeof *id))return 0;
    uint8_t *p=output;
    if(gg_scene_encode(s,mix,p+32,nested)!=nested)return 0;
    memset(p,0,32);memcpy(p,"BBKS",4);p[4]=id->kind?2:1;p[28]=id->kind;
    p[5]=id->kit;p[6]=id->echo;p[7]=id->spring;
    bb_wire_put32(p+8,(uint32_t)bytes);
    bb_wire_put32(p+16,id->pack_header_crc);bb_wire_put32(p+20,id->pack_payload_crc);
    uint32_t gain;memcpy(&gain,&id->gain,4);bb_wire_put32(p+24,gain);
    bb_wire_put32(p+12,sample_scene_crc(p,bytes));return bytes;
}
int bb_sample_scene_decode(const void *input,size_t bytes,uint32_t rate,
                           uint32_t header,uint32_t payload,unsigned count,
                           bb_family_scene_workspace *w,uint8_t *mix,
                           bb_sample_scene_identity *id){
    if(!input||!w||!mix||!id||!count||count>8||
       (bytes!=BB_SESSION_BYTES+32 && bytes!=BB_SESSION_BYTES+GG_SCENE_OVERHEAD+32)||
       !bb_wire_separated(input,bytes,w,sizeof *w)||
       !bb_wire_separated(input,bytes,mix,16)||!bb_wire_separated(input,bytes,id,sizeof *id)||
       !bb_wire_separated(w,sizeof *w,mix,16)||!bb_wire_separated(w,sizeof *w,id,sizeof *id)||
       !bb_wire_separated(mix,16,id,sizeof *id))return 0;
    const uint8_t *p=input;
    if(memcmp(p,"BBKS",4)||!((p[4]==1&&p[28]==0)||(p[4]==2&&p[28]==1))||p[5]>=sample_scene_kits(p,header,payload,count)||
       bb_wire_get32(p+8)!=bytes||bb_wire_get32(p+12)!=sample_scene_crc(p,bytes)||p[29]||p[30]||p[31])return 0;
    bb_sample_scene_identity next={0};next.pack_header_crc=header;next.pack_payload_crc=payload;
    next.kit=p[5];next.echo=p[6];next.spring=p[7];next.kind=p[28];
    uint32_t gain=bb_wire_get32(p+24);memcpy(&next.gain,&gain,4);
    uint8_t staged_mix[16];
    if(!sample_scene_identity_valid(&next)||
       !gg_scene_decode_staged(p+32,bytes-32,rate,w,staged_mix))return 0;
    for(unsigned k=0;k<4;k++)if(w->decoded.family[k])return 0;
    memcpy(mix,staged_mix,16);*id=next;return 1;
}
_Static_assert(BB_SAMPLE_PAIR_SCENE_BYTES<=3840u,"pair fits existing journal bound");
size_t bb_sample_pair_scene_encode(const bb_family_scene *s,const uint8_t *mix,
                                  const bb_sample_scene_identity id[2],void *output,size_t capacity){
    if(!s||!mix||!output||!id||!sample_scene_identity_valid(id)||
       !sample_scene_identity_valid(id+1)||!bb_family_scene_valid(s)||
       id[0].pack_header_crc!=id[1].pack_header_crc||id[0].pack_payload_crc!=id[1].pack_payload_crc)return 0;
    for(unsigned k=0;k<4;k++)if(s->family[k])return 0;
    int extended=0;for(unsigned k=0;k<16;k++)extended|=mix[k]!=127;
    size_t nested=BB_SESSION_BYTES+(extended?GG_SCENE_OVERHEAD:0),bytes=nested+48;
    if(capacity<bytes||!bb_wire_separated(output,bytes,s,sizeof *s)||
       !bb_wire_separated(output,bytes,mix,16)||!bb_wire_separated(output,bytes,id,2*sizeof *id))return 0;
    uint8_t *p=output;if(gg_scene_encode(s,mix,p+48,nested)!=nested)return 0;
    memset(p,0,48);memcpy(p,"BBKS",4);p[4]=3;
    p[5]=id[0].kit;p[6]=id[0].echo;p[7]=id[0].spring;
    bb_wire_put32(p+8,(uint32_t)bytes);bb_wire_put32(p+16,id[0].pack_header_crc);
    bb_wire_put32(p+20,id[0].pack_payload_crc);
    uint32_t gain;memcpy(&gain,&id[0].gain,4);bb_wire_put32(p+24,gain);
    p[28]=id[0].kind;p[29]=id[1].kind;p[30]=id[1].kit;p[31]=id[1].echo;p[32]=id[1].spring;
    memcpy(&gain,&id[1].gain,4);bb_wire_put32(p+36,gain);
    bb_wire_put32(p+12,sample_scene_crc(p,bytes));return bytes;
}
int bb_sample_pair_scene_decode(const void *input,size_t bytes,uint32_t rate,
                               uint32_t header,uint32_t payload,unsigned count,
                               bb_family_scene_workspace *w,uint8_t *mix,bb_sample_scene_identity id[2]){
    if(!input||!w||!mix||!id||!count||count>8||bytes<32||
       !bb_wire_separated(input,bytes,w,sizeof *w)||!bb_wire_separated(input,bytes,mix,16)||
       !bb_wire_separated(input,bytes,id,2*sizeof *id)||!bb_wire_separated(w,sizeof *w,mix,16)||
       !bb_wire_separated(w,sizeof *w,id,2*sizeof *id)||!bb_wire_separated(mix,16,id,2*sizeof *id))return 0;
    const uint8_t *p=input;bb_sample_scene_identity next[2]={{0}};uint8_t staged_mix[16];
    if(p[4]!=3){
        if(!bb_sample_scene_decode(input,bytes,rate,header,payload,count,w,staged_mix,next))return 0;
        /* Older formats define no B sampled owner: retain its native808 sound.
         * Dormant sample bus defaults must not overwrite native voice sends. */
        next[1].pack_header_crc=header;next[1].pack_payload_crc=payload;
        next[1].kind=1;next[1].gain=BB_SAMPLE_DEFAULT_GAIN;next[1].echo=64;next[1].spring=48;
    }else{
        if((bytes!=BB_SESSION_BYTES+48&&bytes!=BB_SESSION_BYTES+GG_SCENE_OVERHEAD+48)||
           memcmp(p,"BBKS",4)||bb_wire_get32(p+8)!=bytes||
           bb_wire_get32(p+12)!=sample_scene_crc(p,bytes)||
           p[5]>=sample_scene_kits(p,header,payload,count)||p[30]>=sample_scene_kits(p,header,payload,count))return 0;
        for(unsigned i=33;i<36;i++)if(p[i])return 0;
        for(unsigned i=40;i<48;i++)if(p[i])return 0;
        for(unsigned k=0;k<2;k++){
            next[k].pack_header_crc=header;next[k].pack_payload_crc=payload;
            uint32_t gain=bb_wire_get32(p+(k?36:24));memcpy(&next[k].gain,&gain,4);
        }
        next[0].kit=p[5];next[0].echo=p[6];next[0].spring=p[7];next[0].kind=p[28];
        next[1].kind=p[29];next[1].kit=p[30];next[1].echo=p[31];next[1].spring=p[32];
        if(!sample_scene_identity_valid(next)||!sample_scene_identity_valid(next+1)||
           !gg_scene_decode_staged(p+48,bytes-48,rate,w,staged_mix))return 0;
        for(unsigned k=0;k<4;k++)if(w->decoded.family[k])return 0;
    }
    memcpy(mix,staged_mix,16);memcpy(id,next,sizeof next);return 1;
}
