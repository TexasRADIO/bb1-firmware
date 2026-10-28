/* SPDX-License-Identifier: GPL-3.0-only */
/* Exclusive pre-audio_init/usb_start foreground diagnostic. Production DSP,
 * 128-frame cache restarts and full256-frame conversion/copy are measured;
 * local output NEVER reaches DAC/USB. No IRQ, DMA or sustained-play proof. */
#if BB_RICH_BOOT_QUALIFY
_Static_assert(HALF_FRAMES==256,"Rich probe measures the qualified full256-frame DMA half");
static int bb_rich_probe_prepare(unsigned grid,unsigned sampled,bb_phrase_index *out){
    bb_phrase raw={0};raw.lattice=(uint8_t)grid;raw.bars=1;
    raw.length=grid==BB_FDL_EIGHTHS?8:grid==BB_FDL_TRIPLETS?24:16;
    raw.count=9;
    raw.events[0]=BB_FDV_BD|(BB_FDM_FLAM<<13);
    raw.events[1]=BB_FDV_SD|(2u<<5)|(BB_FDI_ACCENTED<<11);
    raw.events[2]=BB_FDV_HH;
    raw.events[3]=BB_FDV_HH|(1u<<5);
    raw.events[4]=BB_FDV_HH|(2u<<5);
    raw.events[5]=BB_FDV_HH|(3u<<5)|(BB_FDM_RETRIGGER_RATE<<13);
    raw.events[6]=BB_FDV_ACCENT|(2u<<5);
    raw.events[7]=BB_PHRASE_SHAKER|(BB_FDI_UNACCENTED<<11);
    raw.events[8]=BB_FDV_CP|(4u<<5);
    /* Sampled banks have no synthetic shaker. Keep the kick flam, snare
     * accent, closed-hat retrigger and clap; never substitute a missing voice. */
    if(sampled){raw.events[7]=raw.events[8];raw.events[8]=0;raw.count=8;}
    if(sampled>1u)raw.events[raw.count++]=BB_FDV_RD;
    return bb_phrase_index_prepare(&raw,BB_RICH_SOURCE_VOICES,out)&&bb_audio_phrase_preflight(out);
}
static void bb_rich_diagnostic_startup(void){
#if BB_QUIET_STAGE_PROBE
    bb_stage_probe=(bb_stage_probe_report){0};
#endif
    memset(&bb_rich_probe,0,sizeof bb_rich_probe);bb_rich_probe.version=3;
    bb_rich_probe.mix_version=1;
    bb_rich_probe.ride_version=1;
    if(!bb_sample_boot_ready||bb_sample_probe.status!=BB_SP_PASS||
       !bb_single_startup_admitted()||bb_sound.sample!=&bb_sample_store.binding||bb_sample_musical_count!=3){
        bb_rich_probe.status=BB_RP_ADMISSION;return;
    }
    bb_rich_probe.routes=BB_RP_ROUTES;bb_rich_probe.min_ticks=UINT32_MAX;
    bb_rich_probe.trace_version=1;
    uint32_t began=fm1_ticks(),jobs=bb_c1_live.jobs;
    static const uint8_t grids[BB_RP_ROUTES][2]={
        {BB_FDL_EIGHTHS,BB_FDL_TRIPLETS},{BB_FDL_TRIPLETS,BB_FDL_SIXTEENTHS},
        {BB_FDL_SIXTEENTHS,BB_FDL_TRIPLETS},{BB_FDL_SIXTEENTHS,BB_FDL_EIGHTHS},
        {BB_FDL_EIGHTHS,BB_FDL_TRIPLETS},{BB_FDL_TRIPLETS,BB_FDL_SIXTEENTHS},
        {BB_FDL_SIXTEENTHS,BB_FDL_EIGHTHS}};
    for(unsigned route=0;route<BB_RP_ROUTES;route++){
        if(!bb_single_startup_restore()||!bb_sample_rebind_cold()){
            bb_rich_probe.status=BB_RP_RESTORE;break;
        }
        bb_private_block_active=1;
        if(!bb_set_tempo(&bb_sound.core,300)||!bb_set_swing(&bb_sound.core,route&1?75:50)||
           !bb_set_enabled(&bb_sound.core,1,1)||!bb_set_synth_length(&bb_sound.core,0,4)||
           !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_ECHO,64)||
           !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_SPRING,48)||
           !bb_fx_parameter_set(&bb_sound.core,BB_FX_ECHO_WET,70)||
           !bb_fx_parameter_set(&bb_sound.core,BB_FX_SPRING_WET,60)){
            bb_rich_probe.status=BB_RP_SOURCE;break;
        }
        for(unsigned m=0;m<2;m++)memset(bb_sound.core.drums[m].steps,0,sizeof bb_sound.core.drums[m].steps);
        for(unsigned step=0;step<4;step++)bb_sound.core.synths[0].notes[step]=(bb_note){(uint8_t)(36+step*3),1,(uint8_t)(step&1),(uint8_t)(step==3)};
        bb_phrase_index phrase;uint32_t ticket=0;
        unsigned sampled_rich=route>=4,selected=sampled_rich?route-4:0;
        uint32_t sample_serial=bb_sound.sample_select_serial;
        if(sampled_rich&&!bb_command_push(&bb_sound.commands,&(bb_command){.type=BB_CMD_SAMPLE_KIT,.value=(uint16_t)selected})){
            bb_rich_probe.status=BB_RP_SOURCE;break;
        }
        for(unsigned m=0;m<2;m++){
            if(route==2&&!m){
                if(!bb_set_length(&bb_sound.core,0,4)||
                   !bb_command_push(&bb_sound.commands,&(bb_command){.type=BB_CMD_SAMPLE_KIT,.value=0})){
                    bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                for(unsigned step=0;step<4;step++)bb_sound.core.drums[0].steps[step]=(bb_drum_step){(uint16_t)((1u<<10)|(1u<<(step&1))),0};
                continue;
            }
            if(!bb_rich_probe_prepare(grids[route][m],sampled_rich&&!m,&phrase)||!bb_audio_phrase_submit(&bb_sound,m,&phrase,16,&ticket)){
                bb_rich_probe.status=BB_RP_SOURCE;break;
            }
        }
        if(bb_rich_probe.status)break;
        bb_start(&bb_sound.core);
        uint32_t dillo=0,wet=0,transition=0,ride_ticket=0;
        unsigned ride_applied=0,ride_slot_seen=0;
        bb_source_tone_view source_views[2]={{0}};
        for(unsigned block=0;block<BB_RP_BLOCKS;block++){
            /* Producer preparation is outside the timing bracket. Admission,
             * copied payload consumption and boundary application remain in
             * measured production callbacks. Original16-tick launch policy. */
            /* Source edits are produced outside the audio bracket, but their
             * actual admission/core/DSP synchronization remains inside it.
             * Total snapshot-call cost conservatively includes the guard,
             * not just the protected copy. This remains a QUIET diagnostic. */
            unsigned edited=(block>=32u && !(block&31u));
            unsigned source=0,values[2]={0,0},submitted=0;
            bb_sample_status mix_before={0};unsigned mix_parameter=0,mix_value=0,mix_submitted=0;
            if(edited){
                static const uint8_t sources[7]={0,11,10,1,13,2,5};
                source=sources[block/32u-1u];
                for(unsigned m=0;m<2;m++){
                    uint32_t began_read=fm1_ticks();
                    int read=bb_audio_source_tone_view_read(&bb_sound,m,&source_views[m]);
                    uint32_t ticks=fm1_ticks()-began_read;
                    if(ticks>bb_rich_probe.source_snapshot_max_ticks)bb_rich_probe.source_snapshot_max_ticks=ticks;
                    if(read<2||!source_views[m].valid){bb_rich_probe.status=BB_RP_SOURCE;break;}
                }
                if(bb_rich_probe.status)break;
                if(sampled_rich){
                    uint32_t began_read=fm1_ticks();
                    int read=bb_audio_sample_status(&bb_sound,&mix_before);
                    uint32_t ticks=fm1_ticks()-began_read;
                    if(ticks>bb_rich_probe.mix_snapshot_max_ticks)bb_rich_probe.mix_snapshot_max_ticks=ticks;
                    if(!read||!mix_before.queue_idle||mix_before.stop_pending||mix_before.selected!=selected){
                        bb_rich_probe.mix_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                    }
                    unsigned index=block/32u-1u,position=index/3u;
                    mix_parameter=index%BB_SAMPLE_MIX_COUNT;
                    mix_value=mix_parameter==BB_SAMPLE_MIX_GAIN?(position?100u:1u):(position==0?0u:position==1?127u:64u);
                }
                for(unsigned m=0;m<2;m++){
                    if(!source_views[m].supported)continue; /* sampled A */
                    values[m]=(source_views[m].sound.pot[source][1]+1u)&127u;
                    uint32_t epoch=source_views[m].epoch;
                    bb_command command={.type=BB_CMD_DRUM_SOURCE_PARAMETER,.target=(uint8_t)m,
                        .voice=(uint8_t)source,.step=BB_DRUM_TUNE,.value=(uint16_t)values[m],
                        .note={(uint8_t)epoch,(uint8_t)(epoch>>8),(uint8_t)(epoch>>16),(uint8_t)(epoch>>24)}};
                    uint32_t began_queue=fm1_ticks();
                    int pushed=bb_command_push(&bb_sound.commands,&command);
                    uint32_t ticks=fm1_ticks()-began_queue;
                    if(ticks>bb_rich_probe.source_queue_max_ticks)bb_rich_probe.source_queue_max_ticks=ticks;
                    if(!pushed){bb_rich_probe.status=BB_RP_SOURCE;break;}
                    submitted|=1u<<m;bb_rich_probe.source_commands++;
                }
                if(bb_rich_probe.status)break;
                for(unsigned m=0;m<2;m++)if(submitted&(1u<<m)){
                    if(bb_audio_source_tone_view_read(&bb_sound,m,&source_views[m])!=1||!source_views[m].pending){bb_rich_probe.status=BB_RP_SOURCE;break;}
                    bb_rich_probe.source_pending_reads++;
                }
                if(bb_rich_probe.status)break;
            }
            if(edited&&sampled_rich){
                /* Snapshot was idle before source commands were queued. The
                 * exclusive pre-audio producer cannot race their consumer. */
                bb_command command;uint32_t began_queue=fm1_ticks();
                int pushed=bb_audio_sample_mix_command(&mix_before,mix_parameter,mix_value,&command)&&
                    bb_command_push(&bb_sound.commands,&command);
                uint32_t ticks=fm1_ticks()-began_queue;
                if(ticks>bb_rich_probe.mix_queue_max_ticks)bb_rich_probe.mix_queue_max_ticks=ticks;
                if(!pushed){bb_rich_probe.mix_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;}
                mix_submitted=1;bb_rich_probe.mix_commands++;
                bb_sample_status pending;uint32_t began_read=fm1_ticks();
                int read=bb_audio_sample_status(&bb_sound,&pending);
                ticks=fm1_ticks()-began_read;
                if(ticks>bb_rich_probe.mix_snapshot_max_ticks)bb_rich_probe.mix_snapshot_max_ticks=ticks;
                if(!read||pending.queue_idle||pending.selected!=mix_before.selected||pending.serial!=mix_before.serial||
                   pending.echo!=mix_before.echo||pending.spring!=mix_before.spring||pending.gain!=mix_before.gain){
                    bb_rich_probe.mix_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.mix_pending_reads++;
            }
            if(sampled_rich&&block==16u){
                /* Initial selection was queued before the first phrase.
                 * Admit RD only now, after the sampler actually owns A. */
                if(!bb_rich_probe_prepare(grids[route][0],2,&phrase)||
                   !bb_audio_phrase_submit(&bb_sound,0,&phrase,16,&ride_ticket)){
                    bb_rich_probe.ride_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.ride_requests++;
                bb_audio_phrase_status pending;uint32_t began_read=fm1_ticks();
                int read=bb_audio_phrase_status_read(&bb_sound,0,&pending);
                uint32_t ticks=fm1_ticks()-began_read;
                if(ticks>bb_rich_probe.ride_snapshot_max_ticks)bb_rich_probe.ride_snapshot_max_ticks=ticks;
                if(!read||pending.state.applied==ride_ticket||bb_commands_idle(&bb_sound.commands)){
                    bb_rich_probe.ride_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.ride_pending_reads++;
            }
            if(block==64){
                if(!bb_rich_probe_prepare((grids[route][1]+1u)%3,0,&phrase)||!bb_audio_phrase_submit(&bb_sound,1,&phrase,16,&transition)){
                    bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.transitions++;
            }
#if BB_QUIET_STAGE_PROBE
            if(route==3u&&block==160u)bb_stage_probe_reset(route,block);
#endif
            int32_t output[64];uint32_t faults=bb_audio_faults,start=fm1_ticks();
            for(unsigned frame=0;frame<HALF_FRAMES;frame+=32u){
                if((frame&127u)==0u)bb_target_half_begin();
                bb_target_audio(output,32);
            }
            uint32_t elapsed=fm1_ticks()-start;
#if BB_QUIET_STAGE_PROBE
            if(bb_stage_probe.active)bb_stage_probe_finish(elapsed);
#endif
            bb_rich_probe.blocks++;bb_rich_probe.faults+=bb_audio_faults-faults;
            if(elapsed<bb_rich_probe.min_ticks)bb_rich_probe.min_ticks=elapsed;
            if(elapsed>bb_rich_probe.max_ticks){
                bb_rich_probe.max_ticks=elapsed;
                /* No additional clock reads. Metadata follows the measured
                 * interval and can perturb later cache state; use it to
                 * locate work, never as a clean speed comparison. */
                bb_rich_probe.trace_valid=1;bb_rich_probe.trace_route=route;
                bb_rich_probe.trace_block=block;
                bb_rich_probe.trace_flags=(block==0?1u:0u)|(block==64?2u:0u)|(edited?4u:0u);
                bb_rich_probe.trace_source=edited?source:UINT32_MAX;
                bb_rich_probe.trace_frame_low=(uint32_t)bb_sound.core.frame;
                bb_rich_probe.trace_frame_high=(uint32_t)(bb_sound.core.frame>>32);
                bb_rich_probe.trace_hits_a=bb_sound.hits[0];
                bb_rich_probe.trace_hits_b=bb_sound.hits[1];
            }
            if(elapsed>bb_rich_probe.peaks[route])bb_rich_probe.peaks[route]=elapsed;
            if((block==0||block==64||edited||(sampled_rich&&block==16u))&&elapsed>bb_rich_probe.command_max_ticks)bb_rich_probe.command_max_ticks=elapsed;
            if(bb_audio_faults!=faults||bb_sound.rich.render_errors||bb_audio_entered||bb_half_active||!bb_half_ready||bb_half_at!=128||
               bb_c1_live.failed||bb_c1_live.busy||bb_c1_live.jobs!=jobs){bb_rich_probe.status=BB_RP_RENDER;break;}
            if(elapsed>=HALF_FRAMES*544u){bb_rich_probe.status=BB_RP_BUDGET;break;}
            if(sampled_rich&&(!bb_sound.rich.valid[0]||bb_sound.sample_selected!=selected||
               bb_sound.sample_requested!=selected||bb_sound.sample_select_result!=1||
               bb_sound.sample_select_serial!=sample_serial+1u||bb_sound.sample_rejected)){
                bb_rich_probe.status=BB_RP_SOURCE;break;
            }
            if(sampled_rich&&ride_ticket&&bb_sound.rich.status[0].applied==ride_ticket){
                if(!(bb_sound.rich.active[0].voices&(UINT32_C(1)<<BB_FDV_RD))){
                    bb_rich_probe.ride_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                if(!ride_applied){ride_applied=1;bb_rich_probe.ride_applied++;}
                bb_rich_probe.ride_active_mask|=1u<<selected;
                for(unsigned slot=0;slot<bb_sound.sample->count;slot++)
                    if(bb_sound.sample->slots[slot].voice.active&&bb_sound.sample->slots[slot].note==51u)
                        ride_slot_seen=1;
                if(ride_slot_seen)bb_rich_probe.ride_slot_mask|=1u<<selected;
            }
            if(edited)for(unsigned m=0;m<2;m++)if(submitted&(1u<<m)){
                uint32_t began_read=fm1_ticks();
                int read=bb_audio_source_tone_view_read(&bb_sound,m,&source_views[m]);
                uint32_t ticks=fm1_ticks()-began_read;
                if(ticks>bb_rich_probe.source_snapshot_max_ticks)bb_rich_probe.source_snapshot_max_ticks=ticks;
                if(read!=2||source_views[m].pending||source_views[m].sound.pot[source][1]!=values[m]){
                    bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.source_refreshes++;
            }
            if(bb_rich_probe.status)break;
            if(mix_submitted){
                bb_sample_status applied;uint32_t began_read=fm1_ticks();
                int read=bb_audio_sample_status(&bb_sound,&applied);
                uint32_t ticks=fm1_ticks()-began_read;
                if(ticks>bb_rich_probe.mix_snapshot_max_ticks)bb_rich_probe.mix_snapshot_max_ticks=ticks;
                unsigned echo=mix_parameter==BB_SAMPLE_MIX_ECHO?mix_value:mix_before.echo;
                unsigned spring=mix_parameter==BB_SAMPLE_MIX_SPRING?mix_value:mix_before.spring;
                float gain=mix_parameter==BB_SAMPLE_MIX_GAIN?(float)mix_value*(1.0f/100.0f):mix_before.gain;
                if(!read||!applied.queue_idle||applied.stop_pending||applied.selected!=selected||
                   applied.serial!=mix_before.serial||applied.echo!=echo||applied.spring!=spring||
                   applied.gain!=gain||applied.level!=mix_before.level||bb_sound.sample_edit_rejected){
                    bb_rich_probe.mix_failures++;bb_rich_probe.status=BB_RP_SOURCE;break;
                }
                bb_rich_probe.mix_refreshes++;
            }
            unsigned has_wet=0;
            for(unsigned i=0;i<128;i++){
                uint32_t l,r,b;memcpy(&l,&bb_sound.left[i],4);memcpy(&r,&bb_sound.right[i],4);memcpy(&b,&bb_sound.bass[i],4);
                if((l&0x7f800000u)==0x7f800000u||(r&0x7f800000u)==0x7f800000u||(b&0x7f800000u)==0x7f800000u){bb_rich_probe.status=BB_RP_NONFINITE;break;}
                dillo+=bb_sound.bass[i]!=0;has_wet|=bb_sound.wet_left[i]!=0||bb_sound.wet_right[i]!=0;
            }
            wet+=has_wet;if(bb_rich_probe.status)break;
            if(!(block&31u))fm1_wdt_feed();
            if(fm1_ticks()-began>FM1_TICKS_PER_US*10000000u){bb_rich_probe.status=BB_RP_TIME_LIMIT;break;}
        }
        bb_rich_probe.hits_a+=bb_sound.hits[0];bb_rich_probe.hits_b+=bb_sound.hits[1];
        bb_rich_probe.dillo_frames+=dillo;bb_rich_probe.wet_blocks+=wet;bb_rich_probe.render_errors+=bb_sound.rich.render_errors;
        for(unsigned m=0;m<2;m++){
            bb_rich_probe.primary[m]+=bb_sound.rich.player[m].primary;bb_rich_probe.delayed[m]+=bb_sound.rich.player[m].delayed;
        }
        if(bb_rich_probe.status)break;
        if(!bb_sound.hits[0]||!bb_sound.hits[1]||!dillo||!wet||!transition||
           !bb_sound.rich.valid[1]||bb_sound.rich.status[1].applied!=transition||
           (route!=2&&!bb_sound.rich.valid[0])||(route==2&&bb_sound.rich.valid[0])||
           bb_rich_probe.source_commands!=bb_rich_probe.source_refreshes||
           bb_rich_probe.source_commands!=bb_rich_probe.source_pending_reads){
            bb_rich_probe.status=BB_RP_RENDER;break;
        }
        bb_rich_probe.completed++;
        if(sampled_rich){
            if(!ride_ticket||!ride_applied||!ride_slot_seen){
                bb_rich_probe.ride_failures++;bb_rich_probe.status=BB_RP_RENDER;break;
            }
            bb_rich_probe.sampled_rich_mask|=1u<<selected;
            bb_rich_probe.sampled_rich_hits+=bb_sound.hits[0];
            bb_rich_probe.sampled_rich_acks++;
        }
    }
    bb_rich_probe.total_ticks=fm1_ticks()-began;
    if(!bb_rich_probe.status&&(bb_rich_probe.mix_commands!=21u||bb_rich_probe.mix_pending_reads!=21u||
       bb_rich_probe.mix_refreshes!=21u||bb_rich_probe.mix_failures))bb_rich_probe.status=BB_RP_RENDER;
    if(!bb_rich_probe.status&&(bb_rich_probe.ride_requests!=3u||bb_rich_probe.ride_pending_reads!=3u||
       bb_rich_probe.ride_applied!=3u||bb_rich_probe.ride_active_mask!=7u||bb_rich_probe.ride_slot_mask!=7u||
       bb_rich_probe.ride_failures||!bb_rich_probe.ride_snapshot_max_ticks))bb_rich_probe.status=BB_RP_RENDER;
    if(!bb_rich_probe.status)bb_rich_probe.status=BB_RP_PASS;
    if(!bb_single_startup_restore()||!bb_sample_rebind_cold())bb_rich_probe.status=BB_RP_RESTORE;
    else {
        bb_rich_probe.restored=1;
        bb_sample_status restored;
        if(bb_audio_sample_status(&bb_sound,&restored)&&restored.queue_idle&&!restored.stop_pending&&
           restored.selected==BB_SAMPLE_808_ID&&restored.echo==64&&restored.spring==48&&restored.gain==.25f)
            bb_rich_probe.mix_restored=1;
        else {bb_rich_probe.mix_failures++;bb_rich_probe.status=BB_RP_RESTORE;}
        if(bb_rich_probe.mix_restored&&!bb_sound.rich.valid[0]&&!bb_sound.rich.valid[1])bb_rich_probe.ride_restored=1;
        else {bb_rich_probe.ride_failures++;bb_rich_probe.status=BB_RP_RESTORE;}
    }
}
#else
static void bb_rich_diagnostic_startup(void){}
#endif
