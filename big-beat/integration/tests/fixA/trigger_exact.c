/* SPDX-License-Identifier: GPL-3.0-only
 * L3 fix A: bb_metal_trigger on the compact state equals the firmware's trigger (drum808_trigger ->
 * bb_trigger_selected) on the whole kit, for random CB/CH/OH/CY hits and velocities under every choke mode,
 * interleaved with renders so voices are mid-decay. Compares the complete drum808_t after bb_metal_apply. */
#include <stdio.h>
#include <string.h>
#include "x0x/group_renderer.c"
static const int TRACK[4]={D8_CB,D8_CH,D8_OH,D8_CY};
static uint32_t rng=0x5eedu;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void)
{
    static drum808_t kit; static bb_metal_state ms;
    unsigned checks=0,failures=0,refused=0;
    drum808_prepare_curves(); drum808_init(&kit);
    gg_state g; gg_init(&g);
    for(unsigned step=0;step<20000;step++){
        if(step%500==0){ kit.choke=(int32_t)(rnd()%3);
            for(int k=0;k<4;k++){ drum808_set(&kit,TRACK[k],D8P_TUNE,(int)(rnd()%128)); drum808_set(&kit,TRACK[k],D8P_DECAY,(int)(rnd()%128)); } }
        int k=(int)(rnd()%4); float vel=(rnd()%8==0)?1.0f:(float)(1+rnd()%100)/100.0f;
        drum808_t full=kit; drum808_trigger(&full,TRACK[k],vel);
        drum808_t cmp=kit; bb_metal_extract(&kit,&ms);
        if(!bb_metal_trigger(&ms,k,vel)){refused++;}
        else { bb_metal_apply(&cmp,&ms);
#ifdef MUTATE_TRIGGER
            cmp.lane[L_CB+k%4].hit+=1e-3f;
#endif
            checks++; if(memcmp(&cmp,&full,sizeof full)){ if(failures<5)printf("trigger differs at step %u (lane %d, choke %d)\n",step,k,(int)kit.choke); failures++; } }
        kit=full;
        float d[64]={0},r[64]={0},l[64]={0}; int n=1+(int)(rnd()%64);
        bb_proposal_group_span(&kit,&g,d,r,l,n);
    }
    printf("metal trigger: %u checks, %u refused, %u failures\n",checks,refused,failures);
    return failures!=0;
}
