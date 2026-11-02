/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_live_record.h"
#include "bb_dillo_activity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %u %s\n",__LINE__,#x);exit(1);}}while(0)
static bb_live_record r,copy;
static bb_phrase p,out,guard;
static bb_dillo_activity activity;
static uint32_t counts[BB_DILLO_KIT_VOICES];
/* Independent host-only128-bit reference, never target arithmetic. */
__extension__ typedef unsigned __int128 wide;
static uint64_t rng=UINT64_C(0x84e6ac39105f725b);
static uint64_t random64(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static bb_live_record rational,unchanged;
static bb_phrase empty;
static void phase_cases(void){
 for(unsigned i=0;i<20000;i++){
  memset(&empty,0,sizeof empty);empty.length=(uint8_t)(1+random64()%64);
  uint32_t n=(uint32_t)random64(),d=(uint32_t)random64();if(!n)n=1;if(!d)d=1;
  uint32_t phase=(uint32_t)(random64()%n);unsigned cell=(unsigned)(random64()%empty.length);
  uint64_t delta=i%5?random64():UINT64_MAX;
  memset(&rational,0,sizeof rational);CK(!bb_live_record_begin_phase(&rational,&empty,1,0,0,n,d,phase,cell));
  CK(!bb_live_record_hit(&rational,delta,BB_FDV_BD,0));
  wide position=(wide)delta*d+phase;
  unsigned expected=(unsigned)((((position*2+n)/((wide)n*2))+cell)%empty.length);
  CK(((rational.take.events[0]>>5)&63u)==expected);
 }
 memset(&empty,0,sizeof empty);empty.length=16;empty.bars=1;
 memset(&rational,0,sizeof rational);CK(!bb_live_record_begin_phase(&rational,&empty,1,0,0,11025,2,0,0));
 CK(!bb_live_record_hit(&rational,8268,BB_FDV_BD,0));CK(((rational.take.events[0]>>5)&63u)==1);
 CK(!bb_live_record_undo(&rational));CK(!bb_live_record_begin(&rational,&empty,1,0,0,5512));
 CK(!bb_live_record_hit(&rational,8268,BB_FDV_BD,0));CK(((rational.take.events[0]>>5)&63u)==2);
 CK(!bb_live_record_undo(&rational));unchanged=rational;
 CK(bb_live_record_begin_phase(&rational,&empty,1,0,0,10,0,0,0)==BB_REC_INVALID);CK(!memcmp(&rational,&unchanged,sizeof rational));
 CK(bb_live_record_begin_phase(&rational,&empty,1,0,0,10,3,10,0)==BB_REC_INVALID);CK(!memcmp(&rational,&unchanged,sizeof rational));
 CK(bb_live_record_begin_phase(&rational,&empty,1,0,0,10,3,0,16)==BB_REC_INVALID);CK(!memcmp(&rational,&unchanged,sizeof rational));
 CK(!bb_live_record_begin_phase(&rational,&empty,1,0,100,10,3,2,15));unchanged=rational;
 CK(bb_live_record_hit(&rational,99,BB_FDV_BD,0)==BB_REC_EARLY);CK(!memcmp(&rational,&unchanged,sizeof rational));
 CK(!bb_live_record_hit(&rational,101,BB_FDV_BD,0));CK(((rational.take.events[0]>>5)&63u)==0); /* phase2+3: exact tie wraps */
}
int main(void){
 phase_cases();
 p.length=16;p.bars=1;p.count=1;p.events[0]=(uint16_t)(BB_FDV_SD|(2u<<5)|(BB_FDI_ACCENTED<<11)|(BB_FDM_FLAM<<13));
 CK(!bb_live_record_begin(&r,&p,7,UINT64_C(1)<<4,100,100));
 CK(bb_live_record_begin(&r,&p,7,0,100,100)==BB_REC_BUSY);
 copy=r;CK(bb_live_record_hit(&r,99,BB_FDV_BD,0)==BB_REC_EARLY);CK(!memcmp(&r,&copy,sizeof r));
 CK(!bb_live_record_hit(&r,100,BB_FDV_BD,0));
 CK(!bb_live_record_hit(&r,249,BB_FDV_LT,1));CK(((r.take.events[2]>>5)&63u)==1);
 CK(!bb_live_record_hit(&r,250,BB_FDV_MT,1));CK(((r.take.events[3]>>5)&63u)==2);
 copy=r;CK(bb_live_record_hit(&r,300,BB_FDV_SD,0)==BB_REC_DUPLICATE);CK(!memcmp(&r,&copy,sizeof r));
 CK(r.take.events[0]==p.events[0]);
 CK(bb_live_record_hit(&r,500,BB_FDV_BD,0)==BB_REC_PROTECTED);CK(!memcmp(&r,&copy,sizeof r));
 CK(!bb_live_record_hit(&r,1650,BB_FDV_HH,0));CK(((r.take.events[4]>>5)&63u)==0);
 /* 2.0 review: REC on a hit from this take accents it once; originals never change */
 CK(!bb_live_record_hit(&r,1655,BB_FDV_HH,1));CK(((r.take.events[4]>>11)&3u)==BB_FDI_ACCENTED);CK(r.take.count==5);
 copy=r;CK(bb_live_record_hit(&r,1656,BB_FDV_HH,1)==BB_REC_DUPLICATE);CK(!memcmp(&r,&copy,sizeof r));
 copy=r;CK(bb_live_record_hit(&r,300,BB_FDV_SD,1)==BB_REC_DUPLICATE);CK(!memcmp(&r,&copy,sizeof r));
 CK(bb_live_record_copy(&r,7,&out)==BB_REC_NO_PASS);CK(!bb_live_record_finish(&r));
 memset(&out,0xa5,sizeof out);guard=out;CK(bb_live_record_copy(&r,8,&out)==BB_REC_CONFLICT);CK(!memcmp(&out,&guard,sizeof out));
 CK(!bb_live_record_copy(&r,7,&out));CK(out.count==5);CK(!memcmp(&p.events[0],&out.events[0],2));
 CK(!bb_live_record_undo(&r));CK(!memcmp(&r.take,&p,sizeof p));CK(r.state==BB_REC_IDLE);
 CK(bb_live_record_undo(&r)==BB_REC_NO_PASS);
 p.count=128;for(unsigned i=0;i<128;i++)p.events[i]=(uint16_t)(BB_FDV_SD|((i%16)<<5)|(BB_FDI_ACCENTED<<11));
 CK(!bb_live_record_begin(&r,&p,9,0,0,1));copy=r;
 CK(bb_live_record_hit(&r,UINT64_MAX,BB_FDV_BD,1)==BB_REC_FULL);CK(!memcmp(&r,&copy,sizeof r));CK(!bb_live_record_undo(&r));
 p.count=0;CK(!bb_live_record_begin(&r,&p,9,0,0,1));CK(!bb_live_record_hit(&r,UINT64_MAX,BB_FDV_BD,0));CK(((r.take.events[0]>>5)&63)==15);
 CK(!bb_live_record_finish(&r));r.take.events[0]|=(uint16_t)(BB_FDM_FLAM<<13);CK(bb_live_record_copy(&r,9,&out)==BB_REC_INVALID);
 bb_dillo_activity_reset(&activity);counts[0]=40;bb_dillo_activity_read(&activity,counts,100);CK(!bb_dillo_activity_level(&activity,0,100));
 counts[0]+=3;counts[1]++;bb_dillo_activity_read(&activity,counts,110);CK(activity.pulses[0]==3);CK(bb_dillo_activity_level(&activity,0,110)==4);
 CK(bb_dillo_activity_level(&activity,0,269)==1);CK(!bb_dillo_activity_level(&activity,0,270));
 CK(bb_dillo_activity_pose(0)==BB_DILLO_CURLED);CK(bb_dillo_activity_pose(1)==BB_DILLO_FRONT);CK(bb_dillo_activity_pose(2)==BB_DILLO_SIDE);CK(bb_dillo_activity_pose(15)==BB_DILLO_SHELL);
 counts[0]+=300;bb_dillo_activity_read(&activity,counts,300);CK(activity.pulses[0]==255);
 bb_dillo_activity_reset(&activity);counts[0]=UINT32_MAX;bb_dillo_activity_read(&activity,counts,UINT32_MAX-10);counts[0]=0;
 bb_dillo_activity_read(&activity,counts,UINT32_MAX-5);CK(activity.pulses[0]==1);CK(bb_dillo_activity_level(&activity,0,4)==4);
 bb_dillo_activity_reset(&activity);CK(!bb_dillo_activity_level(&activity,0,0));
 printf("PASS %u recorder/activity checks; recorder=%zuB activity=%zuB (one layer)\n",checks,sizeof r,sizeof activity);return 0;
}
