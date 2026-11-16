/* SPDX-License-Identifier: GPL-3.0-only
 * Allocation/choke/tweak rules from isod89/sloop-fm1 5eb24c1 drums.c. */
#include "sloop_layer.h"
#include <string.h>
static const uint8_t lane_of_gm[47]={
 1,0,7,2,3,8,9,4,9,6,9,5,10,10,11,10,12,11,12,13,11,15,11,13,12,14,14,14,14,14,
 14,14,15,15,13,13,15,15,13,13,7,7,7,14,14,15,15};
void sl_layer_init(sl_layer *l){if(l){memset(l,0,sizeof *l);l->rng=0x1234567u;l->cap=SL_LAYER_VOICES;}}
int sl_layer_set(sl_layer *l,unsigned lane,int tune,int decay){
 if(!l||lane>=SL_TUNE_LANES||tune< -8||tune>7||decay< -4||decay>3)return 0;
 l->tune[lane]=(int8_t)tune;l->dec[lane]=(int8_t)decay;return 1;
}
int sl_layer_pick(const sl_layer *l,unsigned note){
 if(!l||note>127||l->cap>SL_LAYER_VOICES)return -1;
 if(!l->cap)return -2;
 for(unsigned i=0;i<l->cap;i++)if(!l->v[i].active)return (int)i;
 for(unsigned i=0;i<l->cap;i++)if(l->note[i]==note)return (int)i;
 uint32_t best=UINT32_MAX;int chosen=-1;
 for(unsigned i=0;i<l->cap;i++){
  uint32_t kick=l->note[i]==35u||l->note[i]==36u;
  uint32_t key=(kick?0x80000000u:0u)|(l->age[i]&0x7fffffffu);
  if(key<best){best=key;chosen=(int)i;}
 }
 return chosen<0?0:chosen;
}
int sl_layer_set_cap(sl_layer *l,unsigned cap){
 if(!l||cap>SL_LAYER_VOICES||l->cap>SL_LAYER_VOICES)return 0;
 for(unsigned i=cap;i<SL_LAYER_VOICES;i++)if(l->v[i].active){
  l->tail[sl_group(l->note[i])]+=l->last[i];
  memset(&l->v[i],0,sizeof l->v[i]);l->last[i]=0;
 }
 l->cap=(uint8_t)cap;return 1;
}
int sl_layer_hit_bank(sl_layer *l,const sl_bank *bank,unsigned kit,unsigned note,unsigned velocity){
 if(!l||note>127||!velocity||velocity>127||l->cap>SL_LAYER_VOICES)return -1;
 if(!l->cap)return -2;
 int32_t semi;unsigned sound_lane=sl_lane_of_note(note,&semi),crush;
 const uint8_t *record=sl_bank_sound(bank,kit,sound_lane);
 if(!record||!sl_bank_crush(bank,kit,&crush))return -1;
 unsigned lane=note<35?0:note>81?13:lane_of_gm[note-35];
 int tune=l->tune[lane],decay=l->dec[lane];
 if(tune< -8||tune>7||decay< -4||decay>3)return -1;
 if(note==42||note==44)for(unsigned i=0;i<l->cap;i++)if(l->v[i].active&&l->note[i]==46){
  l->v[i].active=0;l->tail[sl_group(l->note[i])]+=l->last[i];l->last[i]=0;
 }
 int i=sl_layer_pick(l,note);if(i<0)return i;
 if(l->v[i].active)l->tail[sl_group(l->note[i])]+=l->last[i];
 l->note[i]=(uint8_t)note;l->age[i]=++l->clock;l->last[i]=0;
 l->rng^=l->rng<<13;l->rng^=l->rng>>17;l->rng^=l->rng<<5;
 if(note==76||note==77)tune=decay=0;
 return sl_voice_on_sound(&l->v[i],(const sl_sound *)(const void *)record,crush,note,velocity,tune*16,(unsigned)(decay+4),l->rng)?i:-1;
}
