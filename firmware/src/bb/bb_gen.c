/* SPDX-License-Identifier: GPL-3.0-only
 * Bjorklund distribution adapted from tensor-programming/fm1-t1,
 * torso_t1_algo.c at bea0fd38fbebe48758059828a7ba107c6248304c.
 * Copyright (C) 2026 Leo Kuroshita, Huegelton Instruments; GPL-3.0-only.
 * Remaining transforms are bounded BB1 control-thread implementations. */
#include "bb_gen.h"
#include <string.h>
typedef struct {
    uint64_t pattern;
    uint8_t  len;
} BjorkGroup;

uint64_t bb_gen_bjorklund(uint8_t steps, uint8_t pulses)
{
    if (steps == 0) return 0;
    if (steps > 64) steps = 64;
    if (pulses == 0) return 0;
    if (pulses >= steps) {
        return (steps == 64) ? ~0ULL : ((1ULL << steps) - 1ULL);
    }

    BjorkGroup groups[64];
    uint8_t count_ones  = pulses;
    uint8_t count_zeros = (uint8_t)(steps - pulses);

    /* Initialize pulses as '1' (len 1) and rests as '0' (len 1) */
    for (uint8_t i = 0; i < count_ones; i++) {
        groups[i].pattern = 1ULL;
        groups[i].len     = 1;
    }
    for (uint8_t i = 0; i < count_zeros; i++) {
        groups[count_ones + i].pattern = 0ULL;
        groups[count_ones + i].len     = 1;
    }

    /* Iterative Bjorklund pairing */
    while (count_zeros > 1) {
        uint8_t min_c = (count_ones < count_zeros) ? count_ones : count_zeros;

        /* Append zero-groups to one-groups */
        for (uint8_t i = 0; i < min_c; i++) {
            groups[i].pattern = (groups[i].pattern << groups[count_ones + i].len) |
                                 groups[count_ones + i].pattern;
            groups[i].len    += groups[count_ones + i].len;
        }

        if (count_ones >= count_zeros) {
            /* Remaining one-groups become the new remainder (zeros group) */
            uint8_t remainder = count_ones - count_zeros;
            for (uint8_t i = 0; i < remainder; i++) {
                groups[min_c + i] = groups[count_zeros + i];
            }
            count_ones  = min_c;
            count_zeros = remainder;
        } else {
            /* Remaining zero-groups shift down */
            uint8_t remainder = count_zeros - count_ones;
            for (uint8_t i = 0; i < remainder; i++) {
                groups[min_c + i] = groups[count_ones + min_c + i];
            }
            count_zeros = remainder;
        }
    }

    /* Concatenate all remaining groups */
    uint8_t total_groups = count_ones + count_zeros;
    uint64_t result_mask = 0ULL;
    uint8_t current_bit = 0;

    for (uint8_t i = 0; i < total_groups; i++) {
        uint64_t pat = groups[i].pattern;
        uint8_t  len = groups[i].len;

        for (int b = len - 1; b >= 0; b--) {
            if ((pat >> b) & 1ULL) {
                result_mask |= (1ULL << current_bit);
            }
            current_bit++;
            if (current_bit >= steps) break;
        }
        if (current_bit >= steps) break;
    }

    return result_mask;
}

static uint64_t gen_mask(unsigned n){return n>=64?UINT64_MAX:((UINT64_C(1)<<n)-1);}
uint64_t bb_gen_euclid(unsigned k,unsigned n,unsigned rotation,unsigned length){
 if(!n||n>64||!length||length>64)return 0;
 uint64_t p=bb_gen_bjorklund((uint8_t)n,(uint8_t)(k>n?n:k)),out=0;
 rotation%=n;for(unsigned s=0;s<length;s++)if((p>>((s+n-rotation)%n))&1u)out|=UINT64_C(1)<<s;
 return out;
}
uint32_t bb_gen_seed(uint32_t seed,unsigned part){uint32_t x=(seed?seed:0x4242474eu)^((part+1u)*0x9e3779b9u);return x?x:1u;}
static uint32_t gen_next(uint32_t *p){uint32_t x=*p?*p:1u;x^=x<<13;x^=x>>17;x^=x<<5;return *p=x;}
static int gen_valid(const bb_phrase *p){bb_phrase_controls c;return p&&bb_phrase_controls_prepare(p,&c);}
uint32_t bb_gen_phrase_voices(const bb_phrase *p){
 if(!gen_valid(p))return 0;uint32_t v=0;
 for(unsigned i=0;i<p->count;i++){unsigned voice=p->events[i]&31u;if(voice<BB_PHRASE_VOICES)v|=UINT32_C(1)<<voice;}return v;
}
uint64_t bb_gen_lane_hits(const bb_phrase *p,unsigned voice,uint64_t *accent){
 uint64_t h=0,a=0;if(gen_valid(p)&&voice<BB_PHRASE_VOICES)for(unsigned i=0;i<p->count;i++){
  uint16_t e=p->events[i];unsigned s=(e>>5)&63u;if((e&31u)==voice){h|=UINT64_C(1)<<s;if(((e>>11)&3u)==1)a|=UINT64_C(1)<<s;}
 }
 if(accent)*accent=a;
 return h;
}
int bb_gen_lane_write(const bb_phrase *p,bb_phrase *out,unsigned voice,uint64_t hits,uint64_t accent,unsigned replace,uint64_t keep){
 if(!out||!gen_valid(p)||voice>=BB_PHRASE_VOICES||voice==BB_FDV_ACCENT||voice==BB_FDV_ACC||replace>1)return 0;
 bb_phrase n=*p;memset(n.events,0,sizeof n.events);n.count=0;uint64_t seen=0;hits&=gen_mask(p->length);keep&=gen_mask(p->length);
 for(unsigned i=0;i<p->count;i++){
  uint16_t e=p->events[i];unsigned s=(e>>5)&63u;uint64_t bit=UINT64_C(1)<<s;
  if((e&31u)==voice){if(!(hits&bit)&&!(keep&bit))continue;seen|=bit;
   if(replace&&!(keep&bit))e=(uint16_t)((e&~(3u<<11))|(((accent&bit)?1u:0u)<<11));
  }
  if(n.count>=BB_FACTORY_DRUM_EVENTS)return 0;n.events[n.count++]=e;
 }
 for(unsigned s=0;s<p->length;s++){uint64_t bit=UINT64_C(1)<<s;if((hits&bit)&&!(seen&bit)&&!(keep&bit)){
  if(n.count>=BB_FACTORY_DRUM_EVENTS)return 0;n.events[n.count++]=(uint16_t)(voice|(s<<5)|(((accent&bit)?1u:0u)<<11));
 }}
 if(!gen_valid(&n))return 0;*out=n;return 1;
}
int bb_gen_phrase_random(const bb_phrase *home,bb_phrase *out,unsigned density,unsigned accent,unsigned anchor,uint32_t seed,uint64_t locked){
 if(!out||!gen_valid(home)||density>100||accent>100||anchor>100)return 0;
 bb_phrase n=*home;uint32_t voices=bb_gen_phrase_voices(home),rng=seed?seed:1u;
 for(unsigned v=0;v<BB_PHRASE_VOICES;v++)if((voices&(UINT32_C(1)<<v))&&v!=BB_FDV_ACCENT&&v!=BB_FDV_ACC){
  if((v==BB_FDV_BD||v==BB_FDV_SD)&&anchor==100)continue;
  uint64_t h=0,a=0,original=bb_gen_lane_hits(home,v,0);
  for(unsigned s=0;s<home->length;s++){uint64_t bit=UINT64_C(1)<<s;unsigned d=density;
   int on=(v==BB_FDV_BD||v==BB_FDV_SD)&&gen_next(&rng)%100<anchor?!!(original&bit):gen_next(&rng)%100<d;
   if(on)h|=bit;if(gen_next(&rng)%100<accent)a|=bit;
  }
  if(!bb_gen_lane_write(&n,&n,v,h,a,1,locked))return 0;
 }
 *out=n;return 1;
}
static int gen_pitch(const bb_evolve_settings *s,unsigned note){return note>=s->low_note&&note<=s->high_note&&(s->scale&(1u<<((note+12u-s->root)%12u)));}
int bb_gen_dillo(const bb_note *home,unsigned length,bb_note *out,const bb_evolve_settings *s,const bb_gen_dillo_args *g){
 if(!home||!out||!s||!g||!length||length>BB_STEPS||s->root>11||!s->scale||s->scale>4095||s->low_note>s->high_note||s->high_note>127||
  g->random>1||g->euclid>1||g->random==g->euclid||g->density>100||g->accent>100||g->slide>100)return 0;
 unsigned notes[128],count=0;for(unsigned n=s->low_note;n<=s->high_note;n++)if(gen_pitch(s,n))notes[count++]=n;if(!count)return 0;
 bb_note next[BB_STEPS];memcpy(next,home,sizeof next);uint32_t rng=g->seed?g->seed:1u;
 for(unsigned i=0;i<BB_STEPS;i++)if(home[i].note>127||home[i].gate>1||home[i].accent>1||home[i].slide>1)return 0;
 for(unsigned i=0;i<length;i++)if(!((g->locked>>i)&1u)){
  unsigned chance=g->density+(i%4?0:14);if(chance>100)chance=100;
  next[i].gate=(uint8_t)(g->euclid?((g->gates>>i)&1u):gen_next(&rng)%100<chance);
  if(next[i].gate){next[i].note=(uint8_t)(g->euclid&&home[i].gate&&gen_pitch(s,home[i].note)?home[i].note:notes[gen_next(&rng)%count]);next[i].accent=(uint8_t)(g->euclid?((g->accents>>i)&1u):gen_next(&rng)%100<g->accent);}else next[i].accent=0;
  next[i].slide=0;
 }
 for(unsigned i=0;i<length;i++)if(!((g->locked>>i)&1u)&&next[i].gate&&next[(i+1)%length].gate)next[i].slide=(uint8_t)(g->random&&gen_next(&rng)%100<g->slide);
 memcpy(out,next,sizeof next);return 1;
}
unsigned bb_gen_changed_steps(const bb_phrase *a,const bb_phrase *b){
 if(!gen_valid(a)||!gen_valid(b)||a->length!=b->length||a->lattice!=b->lattice||a->bars!=b->bars)return BB_STEPS;
 unsigned n=0;for(unsigned s=0;s<a->length;s++){
  unsigned ia=0,ib=0;int diff=0;
  for(;;){while(ia<a->count&&((a->events[ia]>>5)&63u)!=s)ia++;while(ib<b->count&&((b->events[ib]>>5)&63u)!=s)ib++;
   if(ia==a->count||ib==b->count){diff|=(ia!=a->count)||(ib!=b->count);break;}
   diff|=a->events[ia++]!=b->events[ib++];
  }n+=!!diff;
 }return n;
}
void bb_gen_rich_capture(bb_gen_rich *r,const bb_phrase *home,uint32_t seed,unsigned part){
 if(!r)return;
 if(!gen_valid(home)||part>=BB_MACHINES){memset(r,0,sizeof *r);return;}
 bb_phrase keep=*home;memset(r,0,sizeof *r);r->home=keep;r->random=bb_gen_seed(seed,part);r->ready=1;
}
/* Legacy lane bits map to rich source identities, without inventing voices. */
static unsigned gen_lane(unsigned v){static const uint8_t map[BB_PHRASE_VOICES]={0,1,2,3,4,2,3,4,5,5,6,6,7,8,9,10,8,7,7,255,255,6};return v<BB_PHRASE_VOICES?map[v]:255;}
int bb_gen_rich_pass(bb_gen_rich *r,const bb_phrase *cur,bb_phrase *out,const bb_evolve_settings *s,unsigned part){
 if(!r||!out||!s||!r->ready||!gen_valid(cur)||part>=BB_MACHINES||s->amount>100||!s->every_phrases||s->every_phrases>64)return 0;
 *out=*cur;uint32_t pass=r->passes++;
 if(!pass||pass%s->every_phrases||!(s->parts&(1u<<part))||!(s->attributes&(BB_EVOLVE_RHYTHM|BB_EVOLVE_ACCENT))||gen_next(&r->random)%100>=s->amount)return 0;
 uint32_t voices=bb_gen_phrase_voices(&r->home);unsigned choices[BB_PHRASE_VOICES],nv=0;
 for(unsigned v=0;v<BB_PHRASE_VOICES;v++){unsigned lane=gen_lane(v);if((voices&(UINT32_C(1)<<v))&&lane<11&&(s->drum_voices&(1u<<lane)))choices[nv++]=v;}
 if(!nv)return 0;
 for(unsigned attempt=0;attempt<BB_STEPS*2;attempt++){
  unsigned step=gen_next(&r->random)%cur->length;if((s->locked[part]>>step)&1u)continue;
  unsigned v=choices[gen_next(&r->random)%nv];uint64_t accent,hits=bb_gen_lane_hits(cur,v,&accent),bit=UINT64_C(1)<<step;
  if((s->attributes&BB_EVOLVE_RHYTHM)&&(!(s->attributes&BB_EVOLVE_ACCENT)||(gen_next(&r->random)&1u)))hits^=bit;else{if(!(hits&bit))continue;accent^=bit;}
  bb_phrase next;if(!bb_gen_lane_write(cur,&next,v,hits,accent,1,~bit))continue;
  if(bb_gen_changed_steps(&r->home,&next)>BB_EVOLVE_MAX_CHANGED_STEPS||!bb_gen_changed_steps(cur,&next))continue;
  *out=next;return 1;
 }return 0;
}
