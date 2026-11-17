/* SPDX-License-Identifier: GPL-3.0-only */
/* Exclusive pre-audio_init test, after validated sample boot. Local output is
 * NEVER sent to DAC/USB. Measures production callback work, not IRQ/DMA load.
 * Covers HALF_FRAMES of production callbacks per observation, including each
 * 128-frame cache restart. This remains a quiet foreground test: actual DMA,
 * USB routing and interrupt interference require the live IRQ measurements. */
#if BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_TARGET_CANDIDATE)
#include "bb_sample_peak_snapshot.inc"
static void __attribute__((noinline)) bb_sample_diagnostic_startup(void){
 memset(&bb_sample_probe,0,sizeof bb_sample_probe);bb_sample_probe.version=3;
 if(!bb_sample_boot_ready)return;
 if(!bb_single_startup_admitted()||bb_sound.sample!=&bb_sample_store.binding){
  bb_sample_probe.status=BB_SP_ADMISSION;return;
 }
#if defined(BB_SAMPLE_DUAL_TRIAL)
 if(bb_sound.sample_b!=&bb_sample_b_binding||bb_sound.sample_b->slots==bb_sound.sample->slots){
  bb_sample_probe.status=BB_SP_ADMISSION;return;
 }
#endif
 memset(&bb_private_peak,0,sizeof bb_private_peak);
 bb_private_peak.version=1;
 bb_attr_reset(&bb_private_peak_attribution);
 unsigned kits=bb_sample_musical_count;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
 kits++;
#endif
 bb_sample_probe.kits=kits;bb_sample_probe.min_ticks=UINT32_MAX;
 bb_private_block_active=1;
 uint32_t began=fm1_ticks(),jobs=bb_c1_live.jobs;
 const unsigned blocks_per_kit=65536u/HALF_FRAMES;
 const unsigned transition_blocks=8192u/HALF_FRAMES;
 for(unsigned kit=0;kit<kits;kit++){
  unsigned route=kit;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
  route=kit?kit-1:BB_SAMPLE_808_ID;
#endif
  bb_sample_probe.selected_kit=route;
  bb_stop(&bb_sound.core);memset(bb_sample_store.slots,0,sizeof bb_sample_store.slots);
  memset(bb_sound.core.drums[0].steps,0,sizeof bb_sound.core.drums[0].steps);
  const unsigned required=(1u<<0)|(1u<<1)|(1u<<10);
  unsigned lanes=(1u<<BB_DRUM_TRACKS)-1u;
  if(route!=BB_SAMPLE_808_ID)lanes=bb_sound.sample_catalogue_lanes[route];
  if((required&~lanes)||
     !bb_set_length(&bb_sound.core,0,4)||!bb_set_synth_length(&bb_sound.core,0,4)||
     !bb_set_tempo(&bb_sound.core,120)||
     !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_ECHO,64)||
     !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_SPRING,48)||
     !bb_fx_parameter_set(&bb_sound.core,BB_FX_ECHO_WET,70)||
     !bb_fx_parameter_set(&bb_sound.core,BB_FX_SPRING_WET,60)){
   bb_sample_probe.status=BB_SP_SOURCE;break;
  }
  for(unsigned step=0;step<4;step++){
   bb_sound.core.drums[0].steps[step]=(bb_drum_step){(uint16_t)((1u<<10)|(1u<<(step&1))),1};
   bb_sound.core.drums[0].steps[step].accents&=bb_sound.core.drums[0].steps[step].hits;
   bb_sound.core.synths[0].notes[step]=(bb_note){(uint8_t)(36+step*3),1,(uint8_t)(step&1),(uint8_t)(step==3)};
  }
#if defined(BB_TWO_DRUM_TARGET_CANDIDATE)
  /* Independent phrase/state, shared production output and effects budget. */
  memset(bb_sound.core.drums[1].steps,0,sizeof bb_sound.core.drums[1].steps);
  if(!bb_set_length(&bb_sound.core,1,3)||!bb_set_enabled(&bb_sound.core,1,1)){
   bb_sample_probe.status=BB_SP_SOURCE;break;
  }
  bb_sound.core.drums[1].steps[0].hits=1;
  bb_sound.core.drums[1].steps[1].hits=2;
  bb_sound.core.drums[1].steps[2].hits=1u<<10;
  uint32_t second_hits=bb_sound.hits[1];
#endif
  bb_command select={0};select.type=BB_CMD_SAMPLE_KIT;select.value=(uint16_t)route;
  if(!bb_command_push(&bb_sound.commands,&select)){bb_sample_probe.status=BB_SP_SOURCE;break;}
#if defined(BB_SAMPLE_DUAL_TRIAL)
  unsigned route_b=route==BB_SAMPLE_808_ID?BB_SAMPLE_808_ID:(route+1u)%bb_sample_musical_count;
  if(route_b!=BB_SAMPLE_808_ID&&(required&~bb_sound.sample_catalogue_lanes[route_b])){
   bb_sample_probe.status=BB_SP_SOURCE;break;
  }
  bb_command select_b=select;select_b.target=1;select_b.value=(uint16_t)route_b;
  if(!bb_command_push(&bb_sound.commands,&select_b)){bb_sample_probe.status=BB_SP_SOURCE;break;}
#endif
  bb_start(&bb_sound.core);
  uint32_t hits=bb_sound.hits[0],rejections=bb_sound.sample_rejected,dillo=0,wet=0;
  for(unsigned block=0;block<blocks_per_kit;block++){
#if defined(BB_SAMPLE_HYBRID_TRIAL)
   if(kit&&block&&block%transition_blocks==0u){
    /* Force a short overlap at each route boundary: old kick, route, new kick.
     * All commands are consumed INSIDE the measured production callback. */
    bb_command hit={0};hit.type=BB_CMD_DRUM_TRIGGER;hit.voice=0;hit.value=100;
    route=route==BB_SAMPLE_808_ID?kit-1:BB_SAMPLE_808_ID;select.value=(uint16_t)route;
    if(!bb_command_push(&bb_sound.commands,&hit)||!bb_command_push(&bb_sound.commands,&select)||
       !bb_command_push(&bb_sound.commands,&hit)){bb_sample_probe.status=BB_SP_SOURCE;break;}
    bb_sample_probe.transitions++;bb_sample_probe.selected_kit=route;
#if defined(BB_SAMPLE_DUAL_TRIAL)
    hit.target=1;
    route_b=route_b==BB_SAMPLE_808_ID?kit%bb_sample_musical_count:BB_SAMPLE_808_ID;
    select_b.value=(uint16_t)route_b;
    if(!bb_command_push(&bb_sound.commands,&hit)||!bb_command_push(&bb_sound.commands,&select_b)||
       !bb_command_push(&bb_sound.commands,&hit)){bb_sample_probe.status=BB_SP_SOURCE;break;}
#endif
   }
#endif
   int32_t output[64];uint32_t faults=bb_audio_faults,start=fm1_ticks();
   for(unsigned frame=0;frame<HALF_FRAMES;frame+=32u){
    if((frame&127u)==0u)bb_target_half_begin();
    bb_target_audio(output,32);
   }
   uint32_t elapsed=fm1_ticks()-start;
   bb_sample_probe.blocks++;bb_sample_probe.callback_faults+=bb_audio_faults-faults;
   if(elapsed<bb_sample_probe.min_ticks)bb_sample_probe.min_ticks=elapsed;
   if(elapsed>bb_sample_probe.max_ticks){
    bb_sample_probe.max_ticks=elapsed;
    bb_sample_peak_snapshot(kit*blocks_per_kit+block,elapsed);
   }
   if(bb_audio_faults!=faults||bb_audio_entered||bb_half_active||!bb_half_ready||bb_half_at!=128||
      bb_c1_live.failed||bb_c1_live.busy||bb_c1_live.jobs!=jobs){bb_sample_probe.status=BB_SP_RENDER;break;}
   if(elapsed>=HALF_FRAMES*544u){bb_sample_probe.status=BB_SP_BUDGET;break;}
   if(bb_sound.sample_selected!=route||bb_sound.sample_rejected!=rejections){bb_sample_probe.status=BB_SP_REJECTED;break;}
#if defined(BB_SAMPLE_DUAL_TRIAL)
   if(bb_sound.sample_b_selected!=route_b){bb_sample_probe.status=BB_SP_REJECTED;break;}
#endif
#if defined(BB_SAMPLE_HYBRID_TRIAL)
   unsigned active_samples=0;
   for(unsigned slot=0;slot<bb_sound.sample->count;slot++)active_samples|=bb_sound.sample->slots[slot].voice.active;
   unsigned overlap=active_samples&&drum808_active(&bb_sound.kit[0]);
   bb_sample_probe.overlap_blocks+=overlap;
   if(kit&&block&&block%transition_blocks==0u&&!overlap){bb_sample_probe.status=BB_SP_RENDER;break;}
#if defined(BB_SAMPLE_DUAL_TRIAL)
   if(kit&&block&&block%transition_blocks==0u){
    unsigned active_b=0;
    for(unsigned slot=0;slot<bb_sound.sample_b->count;slot++)active_b|=bb_sound.sample_b->slots[slot].voice.active;
    if(!active_b||!drum808_active(&bb_sound.kit[1])){bb_sample_probe.status=BB_SP_RENDER;break;}
   }
#endif
#endif
   /* The renderer's arrays retain the last128 frames. Activity/finite checks
    * below describe that final quantum; elapsed covers the complete half. */
   unsigned has_wet=0;
   for(unsigned i=0;i<128;i++){
    uint32_t l,r,b;memcpy(&l,&bb_sound.left[i],4);memcpy(&r,&bb_sound.right[i],4);memcpy(&b,&bb_sound.bass[i],4);
    if((l&0x7f800000u)==0x7f800000u||(r&0x7f800000u)==0x7f800000u||(b&0x7f800000u)==0x7f800000u){
     bb_sample_probe.status=BB_SP_NONFINITE;break;
    }
    dillo+=bb_sound.bass[i]!=0;has_wet|=bb_sound.wet_left[i]!=0||bb_sound.wet_right[i]!=0;
   }
   wet+=has_wet;
   if(bb_sample_probe.status)break;
   if(!(block&31u))fm1_wdt_feed();
   if(fm1_ticks()-began>FM1_TICKS_PER_US*10000000u){bb_sample_probe.status=BB_SP_TIME_LIMIT;break;}
  }
  bb_sample_probe.sample_hits+=bb_sound.hits[0]-hits;
  bb_sample_probe.sample_rejections+=bb_sound.sample_rejected-rejections;
  bb_sample_probe.dillo_frames+=dillo;bb_sample_probe.wet_blocks+=wet;
  if(bb_sample_probe.status)break;
  if(bb_sound.hits[0]==hits||!dillo||!wet){bb_sample_probe.status=BB_SP_RENDER;break;}
#if defined(BB_TWO_DRUM_TARGET_CANDIDATE)
  if(bb_sound.hits[1]==second_hits||bb_sound.core.drums[1].length!=3||
     bb_sound.core.drums[1].steps[1].hits!=2){bb_sample_probe.status=BB_SP_RENDER;break;}
#endif
  bb_sample_probe.completed_kits++;
 }
 bb_sample_probe.total_ticks=fm1_ticks()-began;
 if(!bb_sample_probe.status)bb_sample_probe.status=BB_SP_PASS;
 /* Preserve diagnostic counters; restore the original stopped default and
  * cold catalogue without rereading or writing the sample region. */
 if(!bb_single_startup_restore()||!bb_sample_rebind_cold())bb_sample_probe.status=BB_SP_RESTORE;
 else bb_sample_probe.restored=1;
}
#else
static void bb_sample_diagnostic_startup(void){}
#endif
