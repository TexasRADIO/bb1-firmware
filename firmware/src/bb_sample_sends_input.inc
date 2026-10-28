/* SPDX-License-Identifier: GPL-3.0-only
 * Sample-layer mix controls. No sample pointers or optimistic owner writes. */
#if BB_SAMPLE_HOST_INTEGRATION && BB_WITH_EFFECTS
/* 0=808/other instrument, 1=sample layer, -1=unavailable: never fall through
 * into an unrelated 808 pot when the owner state cannot be observed. */
static int bb_sample_sends_view(bb_sample_status *view){
    if(!bb_send_page||bb_effect_page||bb_face.requested.page!=BB_DRUMS)return 0;
#if !defined(BB_SAMPLE_DUAL_TRIAL)
    if(bb_face.machine)return 0;
#endif
    if(!bb_sample_ui_count)return 0; /* No registered sampler: retain808 sends. */
    /* A native-only layer can coexist with a registered sample catalogue. */
    if(bb_face.machine==1&&!bb_audio_sample_layer(&bb_sound,1))return 0;
    if(!bb_audio_sample_status_layer(&bb_sound,bb_face.machine,view))return -1;
    if(view->selected==BB_SAMPLE_808_ID)return 0;
    return view->selected<view->count?1:-1;
}
static unsigned bb_sample_sends_value(const bb_sample_status *view,unsigned knob){
    if(knob==0)return view->echo;
    if(knob==1)return view->spring;
    if(knob==2)return view->level;
    return (unsigned)bb_limit((int)(view->gain*100.f+.5f),1,100);
}
static unsigned bb_sample_sends_display(const bb_sample_status *view,unsigned knob){
    unsigned value=bb_sample_sends_value(view,knob);
    return knob==3?value:(value*100u+63u)/127u;
}
static int bb_sample_sends_input(void){
    bb_sample_status view;int route=bb_sample_sends_view(&view);
    if(!route)return 0;
    (void)bb_read_encoder(EN_PRESET); /* No fictitious sample voice picker. */
    for(unsigned k=0;k<4;k++){
        int delta=bb_read_encoder(EN_K1+k);if(!delta)continue;
        bb_dirty=1;
        if(route<0){bb_say("SAMPLE STATE WAIT");continue;}
        if(!view.queue_idle||view.stop_pending){bb_say("MIX BUSY - RETRY");continue;}
        unsigned value=(unsigned)bb_limit((int)bb_sample_sends_value(&view,k)+bb_limit(delta,-127,127),k==3?1:0,k==3?100:127);
        if(k==2){bb_send_machine(view.machine,BB_CMD_DRUM_LEVEL,value);continue;}
        bb_command command;
        unsigned parameter=k==3?BB_SAMPLE_MIX_GAIN:k;
        if(!bb_audio_sample_mix_command(&view,parameter,value,&command)||
           !bb_command_push(&bb_sound.commands,&command)){bb_say("MIX BUSY - RETRY");continue;}
        bb_say("SAMPLE MIX REQUESTED");
    }
    return 1;
}
#endif
