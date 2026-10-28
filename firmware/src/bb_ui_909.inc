/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_sequence.h"
static uint8_t bb_ui_909_page;
#include "bb_ui_kit_label.h"
/* r9.9: a sampled kit reads as on the KIT page (LinnDrum, DrumTraks, DMX), not the native kit it last had. */
static const char *bb_ui_kit_name(unsigned layer){
    const char *native=bb_drum_kit_names[bb_face.requested.drums[layer].kit];
#if BB_WITH_BROWSER && BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_HYBRID_TRIAL)
    bb_sample_status s;
    int ok=bb_sound_pick.sample_count&&bb_audio_sample_status_layer(&bb_sound,layer,&s);
    return bb_ui_kit_label(bb_kit_family[layer]==1,native,bb_sound_pick.sample_names,bb_sound_pick.sample_count,
                           ok,ok?s.selected:BB_SAMPLE_808_ID,BB_SAMPLE_808_ID);
#else
    return bb_ui_kit_label(bb_kit_family[layer]==1,native,0,0,0,0,0);
#endif
}
static const char *bb_ui_voice_name(void){
    return bb_kit_family[bb_face.machine]==1?(bb_face.voice==11?"OH NINE OH / KIT":bb909_sequence_voice_name(bb_face.voice)):
        bb_drum_voice_name(&bb_face.requested.drums[bb_face.machine].sound,bb_face.voice);
}
static int bb_ui_kit_choose(int delta){
    unsigned layer=bb_face.machine;
    int current=bb_kit_family[layer]==1?BB_DRUM_KITS:bb_face.requested.drums[layer].kit;
    int next=bb_limit(current+bb_limit(delta,-BB_DRUM_KITS,BB_DRUM_KITS),0,BB_DRUM_KITS-1);
    if(next==current)return 0;
    if(!bb_kit_switch_request(layer,next==BB_DRUM_KITS?1u:0u,next==BB_DRUM_KITS?0u:(unsigned)next)){
        bb_say(bb_kit_switch_message?bb_kit_switch_message:"KIT NOT AVAILABLE");return 0;
    }
    bb_say("PREPARING KIT");return 1;
}
static unsigned bb_ui_909_parameter(unsigned knob){
    unsigned count=bb909_control_count(bb_face.voice),pages=(count+3u)/4u;
    return pages?4u*(bb_ui_909_page%pages)+knob:BB909_CONTROL_POTS;
}
static int bb_ui_909_knob(unsigned knob,int delta){
    unsigned voice=bb_face.voice,parameter=bb_ui_909_parameter(knob);
    int max=bb909_control_max(voice,parameter);
    if(max<0){bb_say("NO CONTROL");return 0;}
    uint32_t epoch=bb909_audio_epoch();
    if(!epoch){bb_say("909 NOT READY");return 0;}
    bb_command c={0};c.type=BB_CMD_909_PARAMETER;c.target=bb_face.machine;
    c.voice=(uint8_t)voice;c.step=(uint8_t)parameter;
    c.value=(uint16_t)bb_limit((int)bb_c1_909_controls.value[voice][parameter]+bb_limit(delta,-127,127),0,max);
    c.note=(bb_note){(uint8_t)epoch,(uint8_t)(epoch>>8),(uint8_t)(epoch>>16),(uint8_t)(epoch>>24)};
    if(!bb_command_push(&bb_sound.commands,&c)){bb_say("BUSY / RETRY");return 0;}
    bb_dirty=1;return 1; /* applied values publish only after checked audio */
}
