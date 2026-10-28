/* SPDX-License-Identifier: GPL-3.0-only */
/* PRE-AUDIO ONLY. Exact production callback into a local output buffer.
 * Keeps the existing worker deadline, experiment cap, watchdog and clocks.
 * Measures render/conversion/copy cost; physical IRQ/DMA/UI remain unmeasured.
 * Kind7 is distinct from kind6 parity. No remote/live transport is started.
 * Must restore factory state and persistent owner before audio_init(). */
static void pab_target_full_arrangement(void){
    if(bb_private_render_result.kind!=6 || bb_private_render_result.status!=BB_PR_PASS ||
       !bb_private_render_admitted())return; /* Keep the original startup failure intact. */
    _Static_assert(sizeof(bb_c1_phase_pool)>=sizeof(bb_group_runtime)+BB_MACHINES*sizeof(gg_state),
        "pre-audio restore uses existing pool tail");
    unsigned char *saved=(unsigned char *)&bb_c1_phase_pool+sizeof(bb_group_runtime);
    bb_c1_drum_copy(saved,bb_sound.groups,BB_MACHINES*sizeof(gg_state));
    uint32_t saved_core=bb_c1_drum_hash(PO_GUARD,&bb_sound.core,sizeof bb_sound.core);
    memset(&bb_private_render_result,0,sizeof bb_private_render_result);
    memset(&bb_private_peak,0,sizeof bb_private_peak);bb_private_peak.version=1;
    bb_private_render_result.version=7;bb_private_render_result.kind=9; /* denser independent B, same physical dispatch contract */
    bb_private_render_result.min_ticks=UINT32_MAX;
    /* Split startup parity already completed; do not run legacy owner tests. */
    bb_private_block_active=1;
    /* Two independent drum patterns plus both synths and active echo/spring. */
    if(!bb_set_enabled(&bb_sound.core,1,1)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_ECHO,72)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_DILLO_SPRING,60)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_TELEGRAM_ECHO,65)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_TELEGRAM_SPRING,55)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_ECHO_WET,70)||
       !bb_fx_parameter_set(&bb_sound.core,BB_FX_SPRING_WET,60)){
        bb_private_render_result.status=BB_PR_SOURCE;goto restore;
    }
    /* Pre-audio musical coverage: a second independent 16-step pattern,
     * not merely enabling the sparse factory kick. Retain all original
     * synthesis, effects, event-span timing and three-second cap. */
    bb_sound.core.drums[1].length=16;bb_sound.core.drums[1].next=0;
    for(unsigned step=0;step<16;step++){
        uint16_t hits=(uint16_t)(1u<<D8_CH);
        if(step%4==0)hits|=(uint16_t)(1u<<D8_BD);
        if(step%8==4)hits|=(uint16_t)((1u<<D8_SD)|(1u<<D8_CP));
        if(step%8==6)hits|=(uint16_t)(1u<<D8_OH);
        if(step==3||step==11)hits|=(uint16_t)(1u<<D8_CB);
        if(step==7)hits|=(uint16_t)(1u<<D8_LT);
        if(step==14)hits|=(uint16_t)(1u<<D8_MT);
        if(step==15)hits|=(uint16_t)((1u<<D8_HT)|(1u<<D8_RS));
        if(step==0)hits|=(uint16_t)(1u<<D8_CY);
        bb_sound.core.drums[1].steps[step].hits=hits;
        bb_sound.core.drums[1].steps[step].accents=step%4==0?hits:0;
    }
    bb_browser view=bb_factory_browser;bb_factory_choice choice;char title[48];
    if(!bb_factory_ready||!bb_browser_group(&view,BB_BROWSER_SONGS)||
       !bb_browser_bank_title(&view,9,title,sizeof title)||!bb_private_title_is(title,"Posse On Broadway")||
       !bb_browser_bank_key(&view,9,0)||!bb_browser_part_title(&view,0,title,sizeof title)||
       !bb_private_title_is(title,"Part 1")||!bb_browser_part_key(&view,0,&choice)){
        bb_private_render_result.status=BB_PR_SOURCE;goto restore;
    }
    uint32_t ticket;
    int requested=bb_factory_request_drum_pattern(&bb_sound,&bb_factory_catalogue,&choice,0,16,&ticket);
    bb_private_render_result.source_error=(uint32_t)requested;
    if(requested!=BB_FDRUM_OK){bb_private_render_result.status=BB_PR_REQUEST;goto restore;}
    bb_private_render_result.ticket=ticket;
    /* These are private sequencer flags before audio hardware exists. No
     * live PLAY request is sent, and normal initialization always follows. */
    bb_start(&bb_sound.core);uint32_t begin=fm1_ticks();
    uint32_t initial_jobs=bb_c1_live.jobs,initial_spans=bb_sound.spans;
    /* Diagnostic experiment cap only, not the audio/worker deadline. The
     * existing watchdog is untouched; no feeding or rearming occurs here. */
    for(unsigned block=0;block<1024;block++){
        if(block==512){
            if(!bb_browser_part_title(&view,2,title,sizeof title)||
               !bb_private_title_is(title,"Part 2")||!bb_browser_part_key(&view,2,&choice)){
                bb_private_render_result.status=BB_PR_SOURCE;break;
            }
            requested=bb_factory_request_drum_pattern(&bb_sound,&bb_factory_catalogue,&choice,0,16,
                &bb_private_render_result.transition_ticket);
            bb_private_render_result.source_error=(uint32_t)requested;
            if(requested!=BB_FDRUM_OK){bb_private_render_result.status=BB_PR_REQUEST;break;}
        }
        /* Exact production half routing: rendering, conversion, physical
         * master scaling, visual envelope and four bounded copy-outs. The
         * local output never reaches audio_init, USB audio or a DAC. */
        int32_t private_output[64];uint32_t faults=bb_audio_faults;
        uint32_t start=fm1_ticks();
        bb_half_active=1;bb_half_ready=bb_half_at=0;
        for(unsigned part=0;part<4;part++)bb_target_audio(private_output,32);
        uint32_t elapsed=fm1_ticks()-start;
        int ok=bb_audio_faults==faults&&!bb_audio_entered&&
            !bb_half_active&&bb_half_ready&&bb_half_at==128;
        bb_private_render_result.callback_faults+=bb_audio_faults-faults;
        bb_private_render_result.blocks++;
        if(elapsed>bb_private_render_result.max_ticks){
            bb_private_render_result.max_ticks=elapsed;bb_private_peak_snapshot(block,elapsed,ok&&!bb_c1_live.failed);
        }
        if(!ok||bb_c1_live.failed){bb_private_render_result.status=BB_PR_RENDER;break;}
        /* Complete callback timed; physical DMA/output/IRQ still omitted. */
        if(bb_c1_live.busy||bb_c1_retained_control.active||bb_c1_mail.bad||bb_c1_mail.done!=bb_c1_mail.job){
            bb_private_render_result.status=BB_PR_PROFILE;break;
        }
        bb_drum_pattern_status state;
        if(bb_audio_drum_pattern_status(&bb_sound,0,&state)){
            if(state.state.applied==ticket)bb_private_render_result.applied=ticket;
            if(bb_private_render_result.transition_ticket&&
               state.state.applied==bb_private_render_result.transition_ticket){
                bb_private_render_result.transition_applied=state.state.applied;
                bb_private_render_result.transition_frame=(uint32_t)state.state.applied_frame;
            }
        }
        for(unsigned i=0;i<128;i++){
            uint32_t left,right;memcpy(&left,&bb_sound.left[i],4);memcpy(&right,&bb_sound.right[i],4);
            if((left&0x7f800000u)==0x7f800000u||(right&0x7f800000u)==0x7f800000u)
                bb_private_render_result.status=BB_PR_NONFINITE;
        }
        if(bb_private_render_result.status)break;
        if(UINT32_MAX-bb_private_render_result.render_ticks_sum<elapsed){
            bb_private_render_result.status=BB_PR_PROFILE;break;
        }
        bb_private_render_result.render_ticks_sum+=elapsed;
        if(elapsed<bb_private_render_result.min_ticks)bb_private_render_result.min_ticks=elapsed;
        unsigned echo=0,spring=0,wet=0;
        for(unsigned i=0;i<128;i++){
            echo|=bb_sound.echo_send[i]!=0;spring|=bb_sound.spring_send[i]!=0;
            wet|=bb_sound.wet_left[i]!=0||bb_sound.wet_right[i]!=0;
        }
        bb_private_render_result.send_blocks+=echo&&spring;
        bb_private_render_result.wet_blocks+=wet;
        bb_private_render_result.completed_blocks++;
        if(fm1_ticks()-begin>FM1_TICKS_PER_US*3000000u){bb_private_render_result.status=BB_PR_TIME_LIMIT;break;}
    }
    bb_private_render_result.total_ticks=fm1_ticks()-begin;
    bb_private_render_result.physical_jobs=bb_c1_live.jobs-initial_jobs;
    bb_private_render_result.logical_spans=bb_sound.spans-initial_spans;
    if(!bb_private_block_end()&&
       !bb_private_render_result.status)bb_private_render_result.status=BB_PR_PROFILE;
    if(!bb_private_render_result.status&&bb_private_render_result.completed_blocks!=1024u)
        bb_private_render_result.status=BB_PR_PROFILE;
    if(!bb_private_render_result.status&&(!bb_private_render_result.send_blocks||
       !bb_private_render_result.wet_blocks||!bb_sound.hits[0]||!bb_sound.hits[1]))bb_private_render_result.status=BB_PR_PROFILE;
    if(!bb_private_render_result.status)bb_private_render_result.status=
        bb_private_render_result.applied==ticket&&bb_private_render_result.transition_ticket&&
        bb_private_render_result.transition_applied==bb_private_render_result.transition_ticket?
        BB_PR_PASS:BB_PR_NOT_APPLIED;
    bb_private_render_result.worker_reason=bb_c1_live.failed;
    bb_private_render_result.worker_frames=bb_c1_live.fail_frames;
    bb_private_render_result.worker_trace=bb_c1_live.failed&&bb_c1_live.held?bb_c1_trace:0;
    /* The failed payload is never read, retried or committed here. Any
     * worker failure stays held/latched and requires documented recovery. */
restore:
    if(bb_private_block_active&&!bb_private_block_end()&&!bb_private_render_result.status)
        bb_private_render_result.status=BB_PR_PROFILE;
    bb_private_render_result.drum_b_hits=bb_sound.hits[1];
    if(!bb_private_restore_owner())bb_private_render_result.status=BB_PR_RESTORE;
    bb_c1_drum_copy(bb_sound.groups,saved,BB_MACHINES*sizeof(gg_state));
    if(!bb_c1_live.failed &&
       (saved_core!=bb_c1_drum_hash(PO_GUARD,&bb_sound.core,sizeof bb_sound.core) ||
        !pab_session_reseed(&bb_sound,1) || bb_sound.core.running || bb_sound.core.frame ||
        !bb_commands_idle(&bb_sound.commands) || !pb_ownership_idle(&bb_sound.parts))) {
        bb_private_render_result.status=BB_PR_RESTORE;
        (void)bb_c1_live_fail(BB_C1_LIVE_BOOT_BAD);
    }
    /* Restore public presentation/half state as well as the music owner.
     * Fault evidence remains latched; no recovery or retry happens here. */
    bb_ticks=0;bb_half_active=bb_half_ready=bb_half_at=0;
    memset((void *)bb_playhead,0,sizeof bb_playhead);
    memset((void *)bb_synth_playhead,0,sizeof bb_synth_playhead);
    memset(bb_session_last_output,0,sizeof bb_session_last_output);
    bb_session_resume=0;
}
