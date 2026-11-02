/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.0 kit builder, main-loop module. See bb_user_kit.h for the contract. */
#include "bb_user_kit.h"
#include "bb_voice_fallback.h"
#include "bb_wire.h"
#include "bb_crc32.h"
#include <string.h>

/* Must equal bb_audio.c bb_sample_source_note() (host test asserts it). */
static const uint8_t uk_note[BB_UK_VOICES]={36,38,45,47,50,64,63,62,37,75,39,70,56,49,46,42,51};
/* Phrase voice -> 808 sound (D8S order), as bb_source_trigger.inc. */
static const uint8_t uk_d8s[16]={0,1,2,3,4,5,6,7,8,9,11,10,12,15,14,13};
/* bb_drum_sound_recall(kit 0) decay pots, D8S order (808 Reference). */
static const uint8_t uk_d8_decay[16]={87,108,68,56,53,58,36,34,40,64,51,64,62,47,74,70};
#define UK_808_NATIVE (UINT32_C(65535)|(UINT32_C(1)<<21))
#define UK_FDV_HH 15u
#define UK_FDV_OHH 14u

uint32_t bb_uk_key(char prefix,const char *name,unsigned max){
    uint8_t p[2]={(uint8_t)prefix,':'};
    unsigned n=0;while(name&&n<max&&name[n])n++;
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,2),(const uint8_t *)name,n);
}
static int name_ok(const char *s){
    unsigned end=0;
    for(unsigned i=0;i<BB_UK_NAME;i++){
        unsigned c=(uint8_t)s[i];
        if(!c){end=1;continue;}
        if(end||c<32||c>126)return 0;
    }
    return 1;
}
int bb_uk_valid(const bb_user_kit *k){
    if(!k||!k->machines||k->machines>BB_UK_MACHINES||!name_ok(k->name))return 0;
    uint8_t used=0;
    for(unsigned i=0;i<BB_UK_MACHINES;i++){
        if(i>=k->machines){if(k->key[i])return 0;continue;}
        if(!k->key[i])return 0;
        for(unsigned j=0;j<i;j++)if(k->key[j]==k->key[i])return 0;
    }
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        const bb_uk_voice *e=&k->v[v];
        if(e->machine>=k->machines||e->voice>=BB_UK_VOICES||e->level>127||e->tune>127||e->decay>127)return 0;
        used|=(uint8_t)(1u<<e->machine);
    }
    /* Canonical: every table entry is referenced, in order of first use. */
    if(used!=(uint8_t)((1u<<k->machines)-1u))return 0;
    unsigned next=0;
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        if(k->v[v].machine>next)return 0;
        if(k->v[v].machine==next)next++;
    }
    return 1;
}
static uint32_t record_crc(const uint8_t *p){
    return ~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,8),p+12,BB_UK_RECORD_BYTES-12u);
}
size_t bb_uk_encode(const bb_user_kit *k,uint8_t out[BB_UK_RECORD_BYTES]){
    if(!out||!bb_uk_valid(k)||!bb_wire_separated(out,BB_UK_RECORD_BYTES,k,sizeof *k))return 0;
    memset(out,0,BB_UK_RECORD_BYTES);
    memcpy(out,"BBUK",4);out[4]=BB_UK_VERSION;out[5]=k->machines;
    memcpy(out+12,k->name,BB_UK_NAME);
    for(unsigned i=0;i<BB_UK_MACHINES;i++)bb_wire_put32(out+20+i*4,k->key[i]);
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        uint8_t *q=out+52+v*4;const bb_uk_voice *e=&k->v[v];
        q[0]=(uint8_t)(e->machine|(e->voice<<3));q[1]=e->level;q[2]=e->tune;q[3]=e->decay;
    }
    bb_wire_put32(out+8,record_crc(out));
    return BB_UK_RECORD_BYTES;
}
int bb_uk_decode(const uint8_t in[BB_UK_RECORD_BYTES],bb_user_kit *out){
    if(!in||!out||!bb_wire_separated(in,BB_UK_RECORD_BYTES,out,sizeof *out))return 0;
    if(memcmp(in,"BBUK",4)||in[4]!=BB_UK_VERSION||in[6]||in[7]||
       bb_wire_get32(in+8)!=record_crc(in))return 0;
    bb_user_kit k;memset(&k,0,sizeof k);
    memcpy(k.name,in+12,BB_UK_NAME);k.machines=in[5];
    for(unsigned i=0;i<BB_UK_MACHINES;i++)k.key[i]=bb_wire_get32(in+20+i*4);
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        const uint8_t *q=in+52+v*4;
        k.v[v]=(bb_uk_voice){(uint8_t)(q[0]&7u),(uint8_t)(q[0]>>3),q[1],q[2],q[3]};
    }
    if(!bb_uk_valid(&k))return 0;
    *out=k;return 1;
}
void bb_uk_defaults(const bb_uk_machine *m,unsigned sv,bb_uk_voice *e){
    if(m&&m->engine==BB_UK_ENGINE_808){
        unsigned s=uk_d8s[sv<16?sv:13]; /* RD sounds as CY on the 808 */
        e->level=64;e->tune=64;e->decay=uk_d8_decay[s];
    }else{e->level=BB_UK_FULL;e->tune=BB_UK_TUNE_NATIVE;e->decay=BB_UK_FULL;}
}
/* Rebuild the machine table in order of first use (canonical form). */
static void compact(bb_user_kit *k){
    uint32_t keys[BB_UK_MACHINES]={0};uint8_t map[BB_UK_MACHINES];unsigned n=0;
    memset(map,255,sizeof map);
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        unsigned old=k->v[v].machine;
        if(map[old]==255){map[old]=(uint8_t)n;keys[n++]=k->key[old];}
        k->v[v].machine=map[old];
    }
    memcpy(k->key,keys,sizeof keys);k->machines=(uint8_t)n;
}
int bb_uk_set_source(bb_user_kit *k,unsigned voice,const bb_uk_machine *m,unsigned sv){
    if(!k||!m||!m->key||voice>=BB_UK_VOICES||sv>=BB_UK_VOICES)return 0;
    unsigned i=0;
    while(i<k->machines&&k->key[i]!=m->key)i++;
    if(i==k->machines){
        /* Reuse a slot only this voice was holding, otherwise append. */
        unsigned holders=0,old=k->v[voice].machine;
        for(unsigned v=0;v<BB_UK_VOICES;v++)holders+=k->v[v].machine==old;
        if(holders==1)i=old;
        else if(k->machines<BB_UK_MACHINES)i=k->machines++;
        else return 0;
        k->key[i]=m->key;
    }
    k->v[voice].machine=(uint8_t)i;k->v[voice].voice=(uint8_t)sv;
    bb_uk_defaults(m,sv,&k->v[voice]);
    compact(k);
    return 1;
}
int bb_uk_from_machine(bb_user_kit *k,const bb_uk_machine *m,const char *name){
    if(!k||!m||!m->key)return 0;
    memset(k,0,sizeof *k);
    for(unsigned i=0;name&&i<BB_UK_NAME&&name[i];i++)k->name[i]=name[i];
    k->machines=1;k->key[0]=m->key;
    for(unsigned v=0;v<BB_UK_VOICES;v++){k->v[v].voice=(uint8_t)v;bb_uk_defaults(m,v,&k->v[v]);}
    return bb_uk_valid(k);
}
/* 2^(k/48) in Q16: quarter-semitone tuning without floats or libm. */
static const uint32_t uk_ratio[48]={65536,66489,67456,68438,69433,70443,71468,72507,73562,74632,75717,
    76819,77936,79069,80220,81386,82570,83771,84990,86226,87480,88752,90043,91353,92682,94030,95398,
    96785,98193,99621,101070,102540,104032,105545,107080,108638,110218,111821,113448,115098,116772,
    118470,120194,121942,123715,125515,127341,129193};
static uint32_t tuned_rate(uint32_t rate,unsigned tune){
    if(tune==BB_UK_TUNE_NATIVE)return rate;
    int steps=(int)tune-(int)BB_UK_TUNE_NATIVE;
    int oct=steps>=0?steps/48:-((-steps+47)/48);
    uint64_t r=(uint64_t)rate*uk_ratio[steps-oct*48];
    r=oct>=0?(r<<oct)>>16:(r>>(-oct))>>16;
    return r<1?1u:r>262144u?262144u:(uint32_t)r;
}
/* DECAY on a sample shortens it to the first zero crossing after decay/127 of
 * its length (bb_sample_zero_after, bb_audio.c: one main-loop decode at resolve,
 * sharing the existing IMA tables). The audio path is unchanged. */
static uint32_t decayed_frames(const bb_sample_zone *z,unsigned decay){
    if(decay>=BB_UK_FULL)return z->frames;
    uint32_t target=(uint32_t)(((uint64_t)z->frames*decay)/127u);
    if(target<32u)target=32u;
    return target>=z->frames?z->frames:bb_sample_zero_after(z,target);
}
static const bb_uk_machine *find(const bb_uk_machine *m,unsigned n,uint32_t key){
    for(unsigned i=0;i<n;i++)if(m[i].key==key)return &m[i];
    return 0;
}
static int zone_same(const bb_sample_zone *a,const bb_sample_zone *b){
    return a->data==b->data&&a->bytes==b->bytes&&a->frames==b->frames&&a->rate==b->rate&&a->choke==b->choke;
}
int bb_uk_resolve(const bb_user_kit *k,const bb_uk_machine *inst,unsigned count,
                  uint32_t aux,bb_uk_resolved *out){
    if(!out||!inst||!count||!bb_uk_valid(k))return BB_UK_INVALID;
    const bb_uk_machine *m808=0;
    for(unsigned i=0;i<count;i++)if(inst[i].engine==BB_UK_ENGINE_808){m808=&inst[i];break;}
    if(!m808)return BB_UK_INVALID;  /* the 808 is the floor every reference falls back to */
    bb_uk_resolved r;memset(&r,0,sizeof r);
    uint8_t hat_choke=0;
    for(unsigned v=0;v<BB_UK_VOICES;v++){
        const bb_uk_voice *e=&k->v[v];
        const bb_uk_machine *m=find(inst,count,k->key[e->machine]);
        bb_uk_voice set=*e;
        if(!m){
            m=m808;r.missing|=UINT32_C(1)<<v;r.machines_missing|=(uint8_t)(1u<<e->machine);
            bb_uk_defaults(m,e->voice,&set); /* foreign units mean nothing to the 808 */
        }
        unsigned sv=m->engine==BB_UK_ENGINE_808?bb_voice_fallback(e->voice,UK_808_NATIVE):
                                                bb_voice_fallback(e->voice,m->voices|aux);
        if(sv>=BB_UK_VOICES)return BB_UK_INVALID;
        if(sv!=e->voice)r.substituted|=UINT32_C(1)<<v;
        if(m->engine==BB_UK_ENGINE_808){
            unsigned s=uk_d8s[sv];
            r.engine[v]=BB_UK_ENGINE_808;r.arg[v]=(uint8_t)sv;r.level[v]=127;r.uses808=1;
            if(!(r.pot_mask&(1u<<s))){
                r.pot_mask|=(uint16_t)(1u<<s);
                r.pot[s][0]=set.level;r.pot[s][1]=set.tune;r.pot[s][2]=set.decay;
            }else if(r.pot[s][0]!=set.level||r.pot[s][1]!=set.tune||r.pot[s][2]!=set.decay)
                r.shared808|=UINT32_C(1)<<v;
            continue;
        }
        const bb_sample_zone *src=0;
        for(unsigned z=0;z<m->nz;z++)if(m->zones[z].note==uk_note[sv]){src=&m->zones[z];break;}
        if(!src)return BB_UK_INVALID;
        bb_sample_zone z=*src;
        z.rate=tuned_rate(src->rate,set.tune);z.frames=decayed_frames(src,set.decay);
        /* Keep closed/open hats choking each other across machines. */
        if((v==UK_FDV_HH||v==UK_FDV_OHH)&&z.choke){if(!hat_choke)hat_choke=z.choke;z.choke=hat_choke;}
        unsigned at=r.nz;
        for(unsigned i=0;i<r.nz;i++){bb_sample_zone t=r.zones[i];t.note=z.note;if(zone_same(&t,&z)){at=i;break;}}
        if(at==r.nz){
            if(r.nz>=BB_UK_ZONES)return BB_UK_FULL_ZONES;
            unsigned note=src->note,clash=1;
            while(clash){clash=0;for(unsigned i=0;i<r.nz;i++)if(r.zones[i].note==note){clash=1;note=note<100?100:note+1;break;}}
            if(note>127)return BB_UK_FULL_ZONES;
            z.note=(uint8_t)note;r.zones[r.nz++]=z;
        }
        r.engine[v]=BB_UK_ENGINE_SAMPLE;r.arg[v]=r.zones[at].note;r.level[v]=set.level;r.uses_sample=1;
    }
    *out=r;return BB_UK_OK;
}
int bb_uk_native808(const bb_uk_resolved *r){
    if(!r||r->uses_sample)return 0;
    for(unsigned v=0;v<BB_UK_VOICES;v++)if(r->arg[v]!=bb_voice_fallback(v,UK_808_NATIVE))return 0;
    return 1;
}
unsigned bb_uk_placement(const bb_uk_resolved *r){
    return r&&r->uses808&&r->uses_sample?BB_UK_PLACE_A_ONLY:
           r&&r->uses808&&!bb_uk_native808(r)?BB_UK_PLACE_A_ONLY:BB_UK_PLACE_ANY;
}
static uint32_t bank_crc(const uint8_t *p){return ~bb_crc32_add(UINT32_MAX,p,12);}
size_t bb_uk_bank_encode(const bb_user_kit *const slots[BB_UK_SLOTS],uint8_t out[BB_UK_BANK_BYTES]){
    if(!slots||!out)return 0;
    memset(out,0,BB_UK_BANK_BYTES);uint8_t used=0;
    for(unsigned i=0;i<BB_UK_SLOTS;i++)if(slots[i]){
        if(!bb_uk_encode(slots[i],out+16+i*BB_UK_RECORD_BYTES))return 0;
        used|=(uint8_t)(1u<<i);
    }
    memcpy(out,"BBUB",4);out[4]=1;out[5]=BB_UK_SLOTS;out[6]=used;
    bb_wire_put32(out+8,BB_UK_BANK_BYTES);bb_wire_put32(out+12,bank_crc(out));
    return BB_UK_BANK_BYTES;
}
unsigned bb_uk_bank_decode(const uint8_t in[BB_UK_BANK_BYTES],bb_user_kit slots[BB_UK_SLOTS]){
    if(!in||!slots||memcmp(in,"BBUB",4)||in[4]!=1||in[5]!=BB_UK_SLOTS||in[7]||(in[6]&~15u)||
       bb_wire_get32(in+8)!=BB_UK_BANK_BYTES||bb_wire_get32(in+12)!=bank_crc(in))return 0;
    unsigned used=0;
    for(unsigned i=0;i<BB_UK_SLOTS;i++)
        if((in[6]>>i)&1u&&bb_uk_decode(in+16+i*BB_UK_RECORD_BYTES,&slots[i]))used|=1u<<i;
    return used;
}
