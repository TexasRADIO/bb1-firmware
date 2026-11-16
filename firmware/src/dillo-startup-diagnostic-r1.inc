/* SPDX-License-Identifier: GPL-3.0-only */
/* Only called before audio_init/usb_start. Private sequencing never publishes
 * a live transport command and the local output is never submitted to a DAC.
 * Complete production half rendering/conversion/copy; not IRQ/DMA acceptance. */
#if BB_ONE_DRUM_STUDIO != 1 || BB_WITH_DILLO != 1 || (BB_WITH_TELEGRAM != 0 && BB_WITH_TELEGRAM != 1) || BB_OWNER_AUDIO_BRIDGE
#error "This startup diagnostic is exclusively for the CPU0 drum plus Dillo (and Duo lead) profile"
#endif
static int bb_single_startup_admitted(void) {
    if(bb_private_block_active||bb_audio_entered||bb_audio_faults||
       !bb_c1_live.ready||bb_c1_live.failed||bb_c1_live.busy||
       bb_sound.groups!=bb_single_target_groups||bb_sound.core.running||bb_sound.core.frame||
       bb_sound.core.sample_rate!=44100||!bb_commands_idle(&bb_sound.commands)||
       bb_session_hold_request||bb_session_hold_ack||bb_asset_hold_request||bb_asset_hold_ack||
       bb_kit_hold_request||bb_kit_hold_ack||!bb_one_drum_core_ok(&bb_sound.core))return 0;
#if BB_WITH_TELEGRAM
    /* Dillo Duo: the lead must be silent; this test never loads a lead line and an idle lead is not mixed. */
    if(bb_sound.telegram.amp!=0.0f||bb_sound.telegram.amp_target!=0.0f||bb_sound.telegram.gate_left)return 0;
#endif
    return !bb_sound.core.synths[0].muted&&bb_sound.core.drums[0].enabled&&bb_sound.drum_patterns.family[0]==0;
}
static int bb_single_private_title(const char *a,const char *b) {
    while(*a&&*a==*b){a++;b++;}return *a==*b;
}
static int bb_single_startup_restore(void) {
    /* Reset only this private, pre-audio owner. Preserve fault evidence and
     * never clear/retry worker state, hardware transport or a live queue. */
    bb_target_init();bb_target_trio_init();
    bb_ticks=0;bb_half_active=bb_half_ready=bb_half_at=0;
    memset((void *)bb_playhead,0,sizeof bb_playhead);
    memset((void *)bb_synth_playhead,0,sizeof bb_synth_playhead);
    memset(bb_session_last_output,0,sizeof bb_session_last_output);
    bb_session_resume=0;bb_private_block_active=0;
    return bb_sound.groups==bb_single_target_groups&&!bb_sound.core.running&&!bb_sound.core.frame&&
        bb_commands_idle(&bb_sound.commands)&&bb_one_drum_core_ok(&bb_sound.core)&&
        bb_sound.core.drums[0].enabled;
}
static void __attribute__((noinline)) bb_single_diagnostic_startup(void) {
    memset(&bb_private_render_result,0,sizeof bb_private_render_result);
    bb_private_render_result.version=1;bb_private_render_result.kind=14;
    if(!bb_single_startup_admitted()){
        bb_private_render_result.status=BB_PR_NOT_ADMITTED;return;
    }
    bb_private_render_result.min_ticks=UINT32_MAX;
    bb_private_block_active=1;
    uint32_t jobs=bb_c1_live.jobs,spans=bb_sound.spans,ticket=0,begin=0,dillo_frames=0;
    bb_browser view=bb_factory_browser;bb_factory_choice choice;char title[48];
    if(!bb_fx_parameter_set(&bb_sound.core,BB_FX_ECHO_WET,70)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_SPRING_WET,60)||
       !bb_set_drum_parameter(&bb_sound.core,0,D8_SD,BB_DRUM_ECHO,64)||
       !bb_set_drum_parameter(&bb_sound.core,0,D8_SD,BB_DRUM_SPRING,64)){
        bb_private_render_result.status=BB_PR_SOURCE;goto restore;
    }
    if(!bb_factory_ready||!bb_browser_group(&view,BB_BROWSER_SONGS)||
       !bb_browser_bank_title(&view,9,title,sizeof title)||!bb_single_private_title(title,"Posse On Broadway")||
       !bb_browser_bank_key(&view,9,0)||!bb_browser_part_title(&view,0,title,sizeof title)||
       !bb_single_private_title(title,"Part 1")||!bb_browser_part_key(&view,0,&choice)){
        bb_private_render_result.status=BB_PR_SOURCE;goto restore;
    }
    int requested=bb_factory_request_drum_pattern(&bb_sound,&bb_factory_catalogue,&choice,0,16,&ticket);
    bb_private_render_result.source_error=(uint32_t)requested;
    if(requested!=BB_FDRUM_OK){bb_private_render_result.status=BB_PR_REQUEST;goto restore;}
    bb_private_render_result.ticket=ticket;
    bb_start(&bb_sound.core);begin=fm1_ticks(); /* private owner, before audio_init */
    for(unsigned block=0;block<1024;block++) {
        if(block==512) {
            if(!bb_browser_part_title(&view,2,title,sizeof title)||
               !bb_single_private_title(title,"Part 2")||!bb_browser_part_key(&view,2,&choice)){
                bb_private_render_result.status=BB_PR_SOURCE;break;
            }
            requested=bb_factory_request_drum_pattern(&bb_sound,&bb_factory_catalogue,&choice,0,16,
                &bb_private_render_result.transition_ticket);
            bb_private_render_result.source_error=(uint32_t)requested;
            if(requested!=BB_FDRUM_OK){bb_private_render_result.status=BB_PR_REQUEST;break;}
        }
        int32_t private_output[64];uint32_t faults=bb_audio_faults;
        uint32_t start=fm1_ticks();
        bb_half_active=1;bb_half_ready=bb_half_at=0;
        for(unsigned part=0;part<4;part++)bb_target_audio(private_output,32);
        uint32_t elapsed=fm1_ticks()-start;
        bb_private_render_result.blocks++;
        if(elapsed>bb_private_render_result.max_ticks)bb_private_render_result.max_ticks=elapsed;
        bb_private_render_result.callback_faults+=bb_audio_faults-faults;
        if(bb_audio_faults!=faults||bb_audio_entered||bb_half_active||!bb_half_ready||bb_half_at!=128){
            bb_private_render_result.status=BB_PR_RENDER;break;
        }
        if(bb_c1_live.failed||bb_c1_live.busy||bb_c1_live.jobs!=jobs||!bb_one_drum_core_ok(&bb_sound.core)||
           elapsed>=69632u||UINT32_MAX-bb_private_render_result.render_ticks_sum<elapsed){
            bb_private_render_result.status=BB_PR_PROFILE;break;
        }
        bb_drum_pattern_status state;
        if(bb_audio_drum_pattern_status(&bb_sound,0,&state)) {
            if(state.state.applied==ticket)bb_private_render_result.applied=ticket;
            if(bb_private_render_result.transition_ticket&&state.state.applied==bb_private_render_result.transition_ticket){
                bb_private_render_result.transition_applied=state.state.applied;
                bb_private_render_result.transition_frame=(uint32_t)state.state.applied_frame;
            }
        }
        unsigned echo=0,spring=0,wet=0;
        for(unsigned i=0;i<128;i++) {
            uint32_t left,right;memcpy(&left,&bb_sound.left[i],4);memcpy(&right,&bb_sound.right[i],4);
            if((left&0x7f800000u)==0x7f800000u||(right&0x7f800000u)==0x7f800000u){
                bb_private_render_result.status=BB_PR_NONFINITE;break;
            }
            uint32_t bass;memcpy(&bass,&bb_sound.bass[i],4);
            if((bass&0x7f800000u)==0x7f800000u){bb_private_render_result.status=BB_PR_NONFINITE;break;}
            dillo_frames+=bb_sound.bass[i]!=0;
            echo|=bb_sound.echo_send[i]!=0;spring|=bb_sound.spring_send[i]!=0;
            wet|=bb_sound.wet_left[i]!=0||bb_sound.wet_right[i]!=0;
        }
        if(bb_private_render_result.status)break;
        bb_private_render_result.render_ticks_sum+=elapsed;
        if(elapsed<bb_private_render_result.min_ticks)bb_private_render_result.min_ticks=elapsed;
        bb_private_render_result.send_blocks+=echo&&spring;
        bb_private_render_result.wet_blocks+=wet;
        bb_private_render_result.completed_blocks++;
        if(fm1_ticks()-begin>FM1_TICKS_PER_US*3000000u){bb_private_render_result.status=BB_PR_TIME_LIMIT;break;}
    }
    bb_private_render_result.total_ticks=fm1_ticks()-begin;
    bb_private_render_result.physical_jobs=bb_c1_live.jobs-jobs;
    bb_private_render_result.logical_spans=bb_sound.spans-spans;
    if(!bb_private_render_result.status) {
        if(bb_private_render_result.completed_blocks!=1024||!dillo_frames||!bb_sound.hits[0]||bb_sound.hits[1]||
           !bb_private_render_result.send_blocks||!bb_private_render_result.wet_blocks)
            bb_private_render_result.status=BB_PR_PROFILE;
        else bb_private_render_result.status=bb_private_render_result.applied==ticket&&
            bb_private_render_result.transition_ticket&&
            bb_private_render_result.transition_applied==bb_private_render_result.transition_ticket?
            BB_PR_PASS:BB_PR_NOT_APPLIED;
    }
restore:
    bb_private_render_result.drum_b_hits=bb_sound.hits[1];
    if(!bb_single_startup_restore())bb_private_render_result.status=BB_PR_RESTORE;
}
