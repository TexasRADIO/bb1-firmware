/* SPDX-License-Identifier: GPL-3.0-only */
#include "sample_scene.h"
#include "bb_crc32.h"
#include "bb_sample_gain.h"
#include <string.h>
_Static_assert(sizeof(float)==4,"sample scene gain requires binary32");
_Static_assert(BB_SAMPLE_SCENE_BYTES<=3840u,"fits existing project journal");
#define sample_scene_crc(a,b) bb_crc32_record(a,b)
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
/* 1.5 (Joe, 29 Oct: RAISE): a scene saved with the 1.0/1.1 library at that firmware's 0.25 sampled-kit level
 * loads at today's default. Only converted scenes are touched; a level chosen under the current library is kept. */
#define BB_SAMPLE_OLD_DEFAULT_GAIN 0.25f
static float sample_scene_gain(const uint8_t *p,uint32_t header,uint32_t payload,float gain){
    int converted=!(bb_wire_get32(p+16)==header&&bb_wire_get32(p+20)==payload);
    return converted&&gain==BB_SAMPLE_OLD_DEFAULT_GAIN?BB_SAMPLE_DEFAULT_GAIN:gain;
}
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
    uint32_t gain=bb_wire_get32(p+24);memcpy(&next.gain,&gain,4);next.gain=sample_scene_gain(p,header,payload,next.gain);
    uint8_t staged_mix[16];
    if(!sample_scene_identity_valid(&next)||
       !gg_scene_decode_staged(p+32,bytes-32,rate,w,staged_mix))return 0;
    for(unsigned k=0;k<4;k++)if(w->decoded.family[k])return 0;
    memcpy(mix,staged_mix,16);*id=next;return 1;
}
#if BB_WITH_USER_KITS
/* One paired scene codec, with optional per-layer payloads. Legacy output and
 * v3/v4 record order are unchanged; v5 is emitted only for SLOOP. */
_Static_assert(BB_SAMPLE_PAIR_SCENE_BYTES<=3840u,"pair fits existing journal bound");
_Static_assert(BB_SAMPLE_USER_SCENE_BYTES<=3840u,"user-kit base fits journal bound");
int bb_sample_sloop_controls_pack(uint8_t out[14],const int8_t tune[16],const int8_t decay[16]){
    if(!out||!tune||!decay)return 0;
    for(unsigned n=0;n<16;n++)if(tune[n]<-8||tune[n]>7||decay[n]<-4||decay[n]>3)return 0;
    uint8_t p[14]={0};
    for(unsigned n=0;n<16;n++){
        p[n/2]|=(uint8_t)((tune[n]+8)<<((n&1u)*4));
        unsigned bit=n*3,v=(unsigned)(decay[n]+4);
        p[8+bit/8]|=(uint8_t)(v<<(bit%8));
        if(bit%8>5)p[9+bit/8]|=(uint8_t)(v>>(8-bit%8));
    }
    memcpy(out,p,14);return 1;
}
int bb_sample_sloop_controls_unpack(const uint8_t p[14],int8_t tune[16],int8_t decay[16]){
    if(!p||!tune||!decay||!bb_wire_separated(p,14,tune,16)||
       !bb_wire_separated(p,14,decay,16)||!bb_wire_separated(tune,16,decay,16))return 0;
    for(unsigned n=0;n<16;n++){
        tune[n]=(int8_t)((p[n/2]>>((n&1u)*4)&15u)-8);
        unsigned bit=n*3,v=p[8+bit/8];
        if(bit%8>5)v|=(unsigned)p[9+bit/8]<<8;
        decay[n]=(int8_t)((v>>(bit%8)&7u)-4);
    }
    return 1;
}
static int shared_identity_valid(const bb_sample_scene_identity *i,const bb_user_kit *u,const bb_sample_sloop_scene *sl){
    if(!u&&!sl)return sample_scene_identity_valid(i);
    if(!i||i->echo>127||i->spring>127||!(i->gain>0.0f&&i->gain<=1.0f))return 0;
    if(u)return !sl&&i->kind==2&&(i->kit<BB_UK_SLOTS||i->kit==7)&&bb_uk_valid(u);
#if BB_WITH_SLOOP_KITS
    return i->kind==3&&i->kit<31;
#else
    return 0;
#endif
}
size_t bb_sample_shared_scene_encode(const bb_family_scene *s,const uint8_t *mix,const bb_sample_scene_identity id[2],
                                    const bb_user_kit *const user[2],const bb_sample_sloop_scene *const sl[2],void *output,size_t capacity){
    if(!s||!mix||!output||!id||!bb_family_scene_valid(s)||
       id[0].pack_header_crc!=id[1].pack_header_crc||id[0].pack_payload_crc!=id[1].pack_payload_crc)return 0;
    for(unsigned k=0;k<4;k++)if(s->family[k])return 0;
    unsigned um=0,sm=0;size_t records=0;
    for(unsigned k=0;k<2;k++){
        const bb_user_kit *u=user?user[k]:0;const bb_sample_sloop_scene *v=sl?sl[k]:0;
        if(!shared_identity_valid(id+k,u,v))return 0;
        if(u){um|=1u<<k;records+=BB_UK_RECORD_BYTES;}
        if(v){sm|=1u<<k;records+=BB_SAMPLE_SLOOP_RECORD_BYTES;}
    }
    int extended=0;for(unsigned k=0;k<16;k++)extended|=mix[k]!=127;
    size_t nested=BB_SESSION_BYTES+(extended?GG_SCENE_OVERHEAD:0),bytes=48+records+nested;
    if(capacity<bytes||!bb_wire_separated(output,bytes,s,sizeof *s)||!bb_wire_separated(output,bytes,mix,16)||
       !bb_wire_separated(output,bytes,id,2*sizeof *id)||
       (user&&!bb_wire_separated(output,bytes,user,2*sizeof *user))||
       (sl&&!bb_wire_separated(output,bytes,sl,2*sizeof *sl)))return 0;
    for(unsigned k=0;k<2;k++)if(((um>>k&1u)&&!bb_wire_separated(output,bytes,user[k],sizeof *user[k]))||
                               ((sm>>k&1u)&&!bb_wire_separated(output,bytes,sl[k],sizeof *sl[k])))return 0;
    uint8_t *p=output;
    if(gg_scene_encode(s,mix,p+48+records,nested)!=nested)return 0;
    memset(p,0,48);memcpy(p,"BBKS",4);p[4]=sm?5:um?4:3;
    p[5]=id[0].kit;p[6]=id[0].echo;p[7]=id[0].spring;
    bb_wire_put32(p+8,(uint32_t)bytes);bb_wire_put32(p+16,id[0].pack_header_crc);bb_wire_put32(p+20,id[0].pack_payload_crc);
    uint32_t gain;memcpy(&gain,&id[0].gain,4);bb_wire_put32(p+24,gain);
    p[28]=id[0].kind;p[29]=id[1].kind;p[30]=id[1].kit;p[31]=id[1].echo;p[32]=id[1].spring;
    memcpy(&gain,&id[1].gain,4);bb_wire_put32(p+36,gain);p[40]=(uint8_t)um;p[41]=(uint8_t)sm;
    uint8_t *r=p+48;
    for(unsigned k=0;k<2;k++){
        if(um>>k&1u){if(!bb_uk_encode(user[k],r))return 0;r+=BB_UK_RECORD_BYTES;}
        if(sm>>k&1u){bb_wire_put32(r,sl[k]->bank.recipe_crc);bb_wire_put32(r+4,sl[k]->bank.image_crc);memcpy(r+8,sl[k]->controls,14);r+=BB_SAMPLE_SLOOP_RECORD_BYTES;}
    }
    bb_wire_put32(p+12,sample_scene_crc(p,bytes));return bytes;
}
size_t bb_sample_user_scene_encode(const bb_family_scene *s,const uint8_t *mix,const bb_sample_scene_identity id[2],
                                  const bb_user_kit *const user[2],void *output,size_t capacity){
    return bb_sample_shared_scene_encode(s,mix,id,user,0,output,capacity);
}
size_t bb_sample_pair_scene_encode(const bb_family_scene *s,const uint8_t *mix,
                                  const bb_sample_scene_identity id[2],void *output,size_t capacity){
    return bb_sample_shared_scene_encode(s,mix,id,0,0,output,capacity);
}
int bb_sample_shared_scene_decode(const void *input,size_t bytes,uint32_t rate,uint32_t header,uint32_t payload,
                                 unsigned count,bb_family_scene_workspace *w,uint8_t *mix,
                                 bb_sample_scene_identity id[2],bb_user_kit user[2],unsigned *user_mask,
                                 const bb_sample_sloop_bank_identity *bank,bb_sample_sloop_scene sl[2],unsigned *sl_mask){
    if(!input||!w||!mix||!id||!user_mask||!sl_mask||!count||count>8||bytes<48)return 0;
    const void *outs[]={w,mix,id,user,user_mask,sl,sl_mask};
    const size_t sizes[]={sizeof *w,16,2*sizeof *id,2*sizeof *user,sizeof *user_mask,2*sizeof *sl,sizeof *sl_mask};
    for(unsigned i=0;i<7;i++)if(outs[i]){
        if(!bb_wire_separated(input,bytes,outs[i],sizes[i])||
           (bank&&!bb_wire_separated(bank,sizeof *bank,outs[i],sizes[i])))return 0;
        for(unsigned j=0;j<i;j++)if(outs[j]&&!bb_wire_separated(outs[i],sizes[i],outs[j],sizes[j]))return 0;
    }
    const uint8_t *p=input;bb_sample_scene_identity next[2]={{0}};uint8_t staged_mix[16];
    bb_user_kit kits[2];bb_sample_sloop_scene saved[2];unsigned um=0,sm=0;
    if(p[4]!=3&&p[4]!=4&&p[4]!=5){
        if(!bb_sample_scene_decode(input,bytes,rate,header,payload,count,w,staged_mix,next))return 0;
        next[1].pack_header_crc=header;next[1].pack_payload_crc=payload;
        next[1].kind=1;next[1].gain=BB_SAMPLE_DEFAULT_GAIN;next[1].echo=64;next[1].spring=48;
    }else{
        um=p[40];sm=p[41];
        if((um|sm)&~3u||(um&sm)||(um&&!user)||(sm&&(!sl||!bank))||
           p[4]!=(sm?5:um?4:3))return 0;
#if !BB_WITH_SLOOP_KITS
        if(sm)return 0;
#endif
        size_t records=0;
        for(unsigned k=0;k<2;k++)records+=(um>>k&1u)?BB_UK_RECORD_BYTES:(sm>>k&1u)?BB_SAMPLE_SLOOP_RECORD_BYTES:0u;
        unsigned kits_allowed=sample_scene_kits(p,header,payload,count);
        if(!kits_allowed||(bytes!=48+records+BB_SESSION_BYTES&&bytes!=48+records+BB_SESSION_BYTES+GG_SCENE_OVERHEAD)||
           memcmp(p,"BBKS",4)||bb_wire_get32(p+8)!=bytes||bb_wire_get32(p+12)!=sample_scene_crc(p,bytes))return 0;
        for(unsigned i=33;i<36;i++)if(p[i])return 0;
        for(unsigned i=42;i<48;i++)if(p[i])return 0;
        const uint8_t *r=p+48;
        for(unsigned k=0;k<2;k++){
            next[k].pack_header_crc=header;next[k].pack_payload_crc=payload;
            uint32_t gain=bb_wire_get32(p+(k?36:24));memcpy(&next[k].gain,&gain,4);next[k].gain=sample_scene_gain(p,header,payload,next[k].gain);
            if(um>>k&1u){if(!bb_uk_decode(r,&kits[k]))return 0;r+=BB_UK_RECORD_BYTES;}
            if(sm>>k&1u){
                saved[k].bank.recipe_crc=bb_wire_get32(r);saved[k].bank.image_crc=bb_wire_get32(r+4);
                if(saved[k].bank.recipe_crc!=bank->recipe_crc||saved[k].bank.image_crc!=bank->image_crc)return 0;
                memcpy(saved[k].controls,r+8,14);r+=BB_SAMPLE_SLOOP_RECORD_BYTES;
            }
        }
        next[0].kit=p[5];next[0].echo=p[6];next[0].spring=p[7];next[0].kind=p[28];
        next[1].kind=p[29];next[1].kit=p[30];next[1].echo=p[31];next[1].spring=p[32];
        for(unsigned k=0;k<2;k++){
            unsigned u=um>>k&1u,v=sm>>k&1u;
            if(u!=(next[k].kind==2)||v!=(next[k].kind==3)||(!u&&!v&&next[k].kit>=kits_allowed)||
               !shared_identity_valid(next+k,u?&kits[k]:0,v?&saved[k]:0))return 0;
        }
        if(!gg_scene_decode_staged(p+48+records,bytes-48-records,rate,w,staged_mix))return 0;
        for(unsigned k=0;k<4;k++)if(w->decoded.family[k])return 0;
    }
    memcpy(mix,staged_mix,16);memcpy(id,next,sizeof next);
    for(unsigned k=0;k<2;k++){
        if(um>>k&1u)user[k]=kits[k];
        if(sm>>k&1u)sl[k]=saved[k];
    }
    *user_mask=um;*sl_mask=sm;return 1;
}
int bb_sample_user_scene_decode(const void *input,size_t bytes,uint32_t rate,uint32_t header,uint32_t payload,
                               unsigned count,bb_family_scene_workspace *w,uint8_t *mix,
                               bb_sample_scene_identity id[2],bb_user_kit user[2],unsigned *user_mask){
    unsigned sm;return bb_sample_shared_scene_decode(input,bytes,rate,header,payload,count,w,mix,id,user,user_mask,0,0,&sm);
}
int bb_sample_pair_scene_decode(const void *input,size_t bytes,uint32_t rate,
                               uint32_t header,uint32_t payload,unsigned count,
                               bb_family_scene_workspace *w,uint8_t *mix,bb_sample_scene_identity id[2]){
    unsigned um,sm;return bb_sample_shared_scene_decode(input,bytes,rate,header,payload,count,w,mix,id,0,&um,0,0,&sm);
}

#else
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
            uint32_t gain=bb_wire_get32(p+(k?36:24));memcpy(&next[k].gain,&gain,4);next[k].gain=sample_scene_gain(p,header,payload,next[k].gain);
        }
        next[0].kit=p[5];next[0].echo=p[6];next[0].spring=p[7];next[0].kind=p[28];
        next[1].kind=p[29];next[1].kit=p[30];next[1].echo=p[31];next[1].spring=p[32];
        if(!sample_scene_identity_valid(next)||!sample_scene_identity_valid(next+1)||
           !gg_scene_decode_staged(p+48,bytes-48,rate,w,staged_mix))return 0;
        for(unsigned k=0;k<4;k++)if(w->decoded.family[k])return 0;
    }
    memcpy(mix,staged_mix,16);memcpy(id,next,sizeof next);return 1;
}
#endif
