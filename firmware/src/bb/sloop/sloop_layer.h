/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded BB1 layer around actual SLOOP2.8.3 voices; no sequencer or ISR ownership. */
#ifndef SLOOP_LAYER_H
#define SLOOP_LAYER_H
#include "sloop_drums.h"
#include "sloop_bank.h"
#include "group_gain.h"
#define SL_LAYER_VOICES 2u
#define SL_TUNE_LANES 16u
typedef struct {
 uint8_t cap;
 sl_voice v[SL_LAYER_VOICES];uint32_t age[SL_LAYER_VOICES],clock,rng;
 uint8_t note[SL_LAYER_VOICES];int8_t tune[SL_TUNE_LANES],dec[SL_TUNE_LANES];
 int32_t last[SL_LAYER_VOICES],tail[GG_COUNT];
} sl_layer;
void sl_layer_init(sl_layer *);
int sl_layer_pick(const sl_layer *,unsigned);
/* -2 means cap0: reject selection/hit visibly; never report it applied. */
int sl_layer_hit_bank(sl_layer *,const sl_bank *,unsigned,unsigned,unsigned);
int sl_layer_set_cap(sl_layer *,unsigned);
int sl_layer_set(sl_layer *,unsigned,int,int);
static inline unsigned sl_group(unsigned note){
 return note==35||note==36?GG_KICK:note==38||note==39||note==40?GG_SNARE_CLAP:
        note==42||note==44||note==46?GG_HATS:GG_PERCUSSION;
}
#ifndef SL_LAYER_TAP
#define SL_LAYER_TAP(L,slot,buf,n) ((void)0)
#endif
/* Existing BB1 group ramps advance once per frame. Core samples retain their
 * integer identity up to the explicit BB1 float bus conversion. Fixed voice
 * order, bounded cache and declick tails; no scratch proportional to frames. */
static inline int sl_layer_render_mode(sl_layer *l,gg_state *groups,float gain,unsigned echo,unsigned spring,
                                  float *dry,float *delay,float *reverb,unsigned frames,unsigned add){
 if(!l||!groups||!dry||!delay||!reverb||frames>256u||!(gain>=0.0f&&gain<=1.0f)||echo>127||spring>127)return 0;
 for(unsigned i=0;i<SL_LAYER_VOICES;i++)if(!sl_voice_cache_valid(&l->v[i]))return 0;
 float send=(float)echo/127.0f,rev=(float)spring/127.0f;
 for(unsigned off=0;off<frames;off++){
  float gains[GG_COUNT],sum=0;gg_frame(groups,gains);
  for(unsigned g=0;g<GG_COUNT;g++){
   int32_t t=l->tail[g];sum+=gg_voice((float)t*(1.0f/32768.0f),gains[g]);
   l->tail[g]-=t/16+(t>0?1:t<0?-1:0);
  }
  for(unsigned i=0;i<SL_LAYER_VOICES;i++)if(l->v[i].active){
   int32_t sample;
   if(sl_voice_render(&l->v[i],&sample,1)!=1)return 0;
   SL_LAYER_TAP(l,i,&sample,1);
   unsigned g=sl_group(l->note[i]);sum+=gg_voice((float)sample*(1.0f/32768.0f),gains[g]);
   l->last[i]=sample;
   if(!l->v[i].active){l->tail[g]+=sample;l->last[i]=0;}
  }
  float x=sum*gain;
  if(add){dry[off]+=x;delay[off]+=x*send;reverb[off]+=x*rev;}
  else {dry[off]=x;delay[off]=x*send;reverb[off]=x*rev;}
 }
 return 1;
}
static inline int sl_layer_render(sl_layer *l,gg_state *g,float gain,unsigned echo,unsigned spring,
 float *dry,float *delay,float *reverb,unsigned frames){
 return sl_layer_render_mode(l,g,gain,echo,spring,dry,delay,reverb,frames,0);
}
#endif
