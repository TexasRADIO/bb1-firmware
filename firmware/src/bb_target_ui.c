#include "bb_midi_settings_hold.inc"
/* Big Beat first playable panel. Upstream stock UI remains only for recovery.
 * The audio and UI never share writable pattern state. */
#include "bb/bb_panel.h"
#include "bb/bb_fault_notice.h" /* reliability r1: stable notice ids/texts */
#include "home_master/control.c"
#include "home_master/pattern_view.h"
#include "home_master/sound_view.h"
static uint8_t hm_enabled=1;
static void hm_reset(void),hm_enter(void),hm_focus_page(void);
#define HM_MASTER_PAGE (BB_MIX_DIRECT_PAGE|(1u<<8))
#define HM_VISIBLE (hm_enabled && bb_mix_page==BB_MIX_DIRECT_PAGE && bb_pad_page)
#include "bb/group_input.c"
#include "bb_armadillo.h"
#ifndef BB_WITH_BROWSER
#define BB_WITH_BROWSER 0
#endif
#if BB_WITH_BROWSER
#include "bb/bb_factory.h"
#include "bb/bb_player.h"
#include "bb/bb_kit_intent.h"
#include "bb/bb_home_service.h"
#define BB_HOME_PERFORMANCE 1
#define BB_PATTERN_PRIMARY_CONTROL 1
#define BB_PRESET_READABLE_ROWS 1
static uint8_t bb_library_page;
static uint8_t bb_library_panel_pending;
static uint8_t bb_home_refresh;
static bb_player bb_library[BB_SYNTHS];
static bb_player bb_drum_library[BB_MACHINES];
/* Main-loop-only kit intent. A chosen kit is not the audio owner's kit. */
typedef struct {uint8_t valid,id,cursor;} bb_kit_choice;
static bb_kit_choice bb_kit_intent[BB_MACHINES];
static void bb_rich_reset(void);
static void bb_library_reset(void) {
    bb_rich_reset();
    bb_library_panel_pending=0;
    bb_home_refresh=0;
    bb_library_page=0;memset(bb_library,0,sizeof bb_library);memset(bb_drum_library,0,sizeof bb_drum_library);
    memset(bb_kit_intent,0,sizeof bb_kit_intent);
    if(bb_factory_ready)for(unsigned s=0;s<BB_SYNTHS;s++)
        (void)bb_player_init(&bb_library[s],&bb_factory_browser,s);
    if(bb_factory_ready)for(unsigned m=0;m<BB_MACHINES;m++)
        (void)bb_player_init_drum(&bb_drum_library[m],&bb_factory_browser,m);
    (void)bb_home_reset(bb_factory_ready?&bb_factory_browser:NULL,0);
}
#else
#define bb_library_page 0
#endif
static bb_panel bb_face;
static uint8_t bb_dirty=1;
#include "bb_rich_panel.inc"
#define BB_HOLD_GUIDE 1
#include "bb_design_feedback.h"
static bb_help_router bb_design_help;
static uint8_t bb_help_visible,bb_help_tracking,bb_help_used,bb_help_encoder;
static uint32_t bb_help_since;
static int32_t bb_read_encoder(uint32_t role) {
    int32_t delta=panel_enc(role);
    if(delta)bb_help_encoder=1;
    return delta;
}
static uint8_t bb_mix_page;
enum { BB_MIX_BAND_PAGE=1, BB_MIX_KITS_PAGE=2, BB_MIX_DIRECT_PAGE=3 };
static uint8_t bb_diag_page;
static uint8_t bb_tone_page,bb_patch_index[BB_SYNTHS],bb_patch_edited[BB_SYNTHS];
static uint8_t bb_patch_from_scene[BB_SYNTHS];
static uint8_t bb_drum_sound_page,bb_drum_color_page;
static uint8_t bb_kit_page;
#include "bb_sound_picker.h"
/* Enabled for this isolated source candidate; not an installed setting. */
static uint8_t bb_sound_picker_enabled=1;
static uint8_t bb_sound_pick_page,bb_sound_pick_return;
static bb_sound_picker bb_sound_pick;
static uint16_t bb_sound_pick_view;
/* Cold metadata is published only after validated catalogue registration.
 * Both sound browsing and sample sends use this same availability gate. */
#if BB_SAMPLE_HOST_INTEGRATION
static const char *const *bb_sample_ui_names;
static unsigned bb_sample_ui_count;
#else
#define bb_sample_ui_names NULL
#define bb_sample_ui_count 0u
#endif
#define BB_PEER_NAVIGATION 1
#define BB_KIT_SHELF 1
static uint8_t bb_pad_page=1;
#define BB_LIVE_SYNTH_KEYS 1
static uint8_t bb_keys_page[BB_SYNTHS]={1,1},bb_keys_base[BB_SYNTHS]={41,53};
static int8_t bb_pattern_shift[BB_SYNTHS]; /* accepted request, applied by audio */
static uint32_t bb_keys_sent[BB_SYNTHS],bb_keys_blocked,bb_keys_edges,bb_keys_taps,bb_keys_tap_until;
static uint8_t bb_keys_pitch[BB_SYNTHS][27];
static unsigned bb_keys_context=0xffffu;
static uint8_t bb_scope_enabled;
#if BB_WITH_EVOLVE
static uint8_t bb_evo_page,bb_evo_rec,bb_evo_sync_pending;
typedef struct {
    bb_evolve_settings settings;
    uint32_t changes;
    uint8_t ready,enabled,frozen,conflict,pending,result;
} bb_evo_snapshot;
static bb_evo_snapshot bb_evo_view;
#else
#define bb_evo_page 0
#endif
#if BB_WITH_SESSION
static uint8_t bb_scene_page,bb_scene_slot;
#else
#define bb_scene_page 0
#endif
static int bb_scope_visible(void) {
    return bb_scope_enabled && !bb_scene_page && !bb_evo_page && !bb_library_page && !bb_mix_page && !bb_diag_page &&
        !bb_drum_sound_page && !bb_kit_page;
}
#define BB_LIVE_DRUM_PADS 1
#if BB_WITH_EFFECTS
static uint8_t bb_send_page,bb_effect_page,bb_effect_bank;
#define BB_SHARED_EFFECT_CONTROLS 1
#define BB_ECHO_MOTION_CONTROLS 1
#else
/* Captions share the musical draw path with effects-disabled comparisons. */
#define bb_send_page 0
#define bb_effect_page 0
#endif
static unsigned bb_scope_source(void) {
    if(!bb_scope_visible() || bb_send_page || bb_effect_page)return BB_SCOPE_OFF;
    return bb_face.requested.page==BB_DRUMS?BB_SCOPE_KIT_A+bb_face.machine:
        bb_face.requested.page==BB_DILLO?BB_SCOPE_DILLO:BB_SCOPE_TELEGRAM;
}
static void bb_scope_sync(void) {
    unsigned source=bb_scope_source();
    if(bb_sound.scope.source==source)return;
    bb_commands *q=&bb_sound.commands;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    bb_scope_select(&bb_sound.scope,source);
    if(q->leave)q->leave(q->lock_context,token);
}
static uint8_t bb_selected_note[BB_SYNTHS],bb_entry_pitch[BB_SYNTHS]={36,60};
#define BB_SYNTH_CONTROLS 1
#define BB_BAND_MIX_CONTROLS 1
#define BB_MIXER_ROTARIES 1
#define BB_SYNTH_LEVEL_CONTROLS 1
#define BB_UI_LEVEL 255u
#define BB_DRUM_SOUND_CONTROLS 1
#define BB_LENGTH_REQUIRES_REC 1
static int bb_source_tone_context(void){
    return bb_face.requested.page==BB_DRUMS&&bb_rich_active()&&!bb_pad_page&&!bb_kit_page&&!bb_drum_sound_page;
}
static unsigned bb_visible_drum_value(const bb_drum_sound *sound,unsigned parameter){
#if BB_WITH_RICH_DRUM_BROWSER
    if(bb_source_tone_context())return bb_drum_source_parameter_get(sound,bb_rich_sounds[bb_rich_voices[bb_face.machine]],parameter);
#endif
    return bb_drum_parameter_get(sound,bb_face.voice,parameter);
}
#ifndef BB_METER_CPU_Q8
#define BB_METER_CPU_Q8 (song.cpu_q8)
#define BB_METER_MAX_US (audio_max_us)
#define BB_METER_LATE (felucca_dbg.late)
#endif
/* Upstream late counter survives warm resets. Show only this boot's delta,
 * without destroying the retained diagnostic evidence. Init precedes audio. */
static uint32_t bb_late_at_boot;
#define BB_DIAGNOSTICS_BOOT_LOCAL 1
static uint32_t bb_late_this_boot(void){return (uint32_t)(BB_METER_LATE-bb_late_at_boot);}
#ifdef BB_TARGET_TIMING_H
#define BB_DIAGNOSTICS_WHOLE_HALF 1
/* Main-loop display calculation, never in the audio ISR or locked snapshot.
 * Avoid overflowing integer products or adding target 64-bit divide helpers. */
static unsigned bb_deadline_percent(uint32_t ticks,uint32_t budget) {
    float p;
    if(!budget)return 0;
    p=100.0f*(float)ticks/(float)budget;
    return p>=999.0f?999u:(unsigned)(p+0.5f);
}
#endif
static const char *bb_hint="PLAY TO START";
static uint32_t bb_hint_until;
static int bb_limit(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static void bb_say(const char *s){bb_hint=s;bb_hint_until=fm1_ms+1800u;bb_dirty=1;}
#include "bb_ui_909.inc"
#include "aux_navigation_ui.inc"
#include "home_navigation_ui.inc"
static void bb_kit_first_entry(void);
static void bb_nav_boot(void);
static void bb_ui_init(void){hm_reset();bb_sound_pick_page=bb_sound_pick_return=0;memset(&bb_sound_pick,0,sizeof bb_sound_pick);memset(&bb_sound_pick_view,0,sizeof bb_sound_pick_view);an_reset(&bb_aux_return);memset(&bb_aux_fx,0,sizeof bb_aux_fx);memset(&bb_home_gesture,0,sizeof bb_home_gesture);bb_late_at_boot=BB_METER_LATE;bb_panel_init(&bb_face,&bb_sound.core);led_pos_init();bb_help_visible=bb_help_tracking=bb_help_used=0;memset(&bb_design_help,0,sizeof bb_design_help);bb_dirty=1;
#if BB_WITH_BROWSER
    bb_library_reset();
    /* Keep pattern browsers ready; the sound chooser below is the entrance.
     * Opening either browser never selects or replaces musical material. */
    bb_library_page=0;
#endif
    memset(bb_keys_sent,0,sizeof bb_keys_sent);bb_keys_blocked=bb_keys_taps=0;
    bb_keys_context=0xffffu;bb_keys_page[0]=bb_keys_page[1]=1;bb_keys_base[0]=41;bb_keys_base[1]=53;
    memset(bb_pattern_shift,0,sizeof bb_pattern_shift);
    bb_aux_apply(1u<<8);bb_scope_enabled=0;
    /* Navigation v2.1 owns the page from boot: power-on lands on STUDIO HOME. */
    bb_nav_boot();
}
static int bb_send(bb_command_type type,unsigned value) {
    bb_command c={0};c.type=type;c.value=(uint16_t)value;c.target=bb_face.machine;
    bb_dirty=1;
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
    return 1;
}
static int bb_send_machine(unsigned machine,bb_command_type type,unsigned value) {
    bb_command c={0};c.type=type;c.value=(uint16_t)value;c.target=(uint8_t)machine;
    bb_dirty=1;
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
    return 1;
}
#include "group_knobs.inc"
#include "group_mutes.inc"
static unsigned bb_selected_synth(void){return bb_face.requested.page==BB_TELEGRAM?1u:0u;}
/* Main-loop producer only. Retain every accepted key until its release is
 * accepted too. Release old keys before admitting new ones; a full queue
 * defers work to the next input poll rather than losing note-off. */
static void bb_keys_sync(int accept_new) {
    uint32_t held=fm1_in.notes&0x7ffffffu,desired=0;
    unsigned owner=BB_SYNTHS,context=0xffffu;
    int changed;
    uint32_t both=(1u<<panel.btn[B_OCTDN])|(1u<<panel.btn[B_OCTUP]);
    if(bb_face.requested.page!=BB_DRUMS && !bb_mix_page && !bb_diag_page &&
       !bb_scene_page && !bb_evo_page && !bb_library_page && !bb_sound_pick_page && !bb_send_page && !bb_effect_page &&
       !ui.uboot && (fm1_in.buttons&both)!=both) {
        unsigned s=bb_selected_synth();
        if(bb_keys_page[s] && !bb_face.requested.synths[s].muted) {
            owner=s;context=s*128u+bb_keys_base[s];
        }
    }
    bb_keys_blocked&=held;
    changed=context!=bb_keys_context;
    if(changed) {
        bb_keys_context=context;bb_keys_blocked|=held;bb_keys_taps=0;
    }
    if((int32_t)(fm1_ms-bb_keys_tap_until)>=0)bb_keys_taps=0;
    /* A debounced tap may already be up when the main loop sees its edge.
     * Give that tap20ms, then use the same retained release path. */
    if(accept_new && !changed && owner<BB_SYNTHS && (bb_keys_edges&~held)) {
        bb_keys_taps|=bb_keys_edges&~held;bb_keys_tap_until=fm1_ms+20u;
    }
    if(owner<BB_SYNTHS)desired=(held|bb_keys_taps)&~bb_keys_blocked;
    if(bb_keys_sent[0]==(owner==0?desired:0u) &&
       bb_keys_sent[1]==(owner==1?desired:0u))return; /* common idle/held path */
#if BB_WITH_SESSION
    if(bb_session_busy())return; /* no new producer writes during scene hold */
#endif
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned k=0;k<27;k++) {
        uint32_t bit=1u<<k;
        if((bb_keys_sent[s]&bit) && (s!=owner || !(desired&bit))) {
            bb_command c={0};c.type=BB_CMD_SYNTH_KEY;c.target=(uint8_t)s;c.voice=bb_keys_pitch[s][k];
            if(!bb_command_push(&bb_sound.commands,&c)){bb_say("KEY RELEASE WAIT");return;}
            bb_keys_sent[s]&=~bit;bb_dirty=1;
        }
    }
    if(!accept_new || owner>=BB_SYNTHS)return;
    for(unsigned k=0;k<27;k++)if((desired&(1u<<k)) && !(bb_keys_sent[owner]&(1u<<k))) {
        bb_command c={0};c.type=BB_CMD_SYNTH_KEY;c.target=(uint8_t)owner;
        c.voice=(uint8_t)(bb_keys_base[owner]+k);
        c.value=(fm1_in.buttons&(1u<<panel.btn[B_REC]))?127:88;
        if(!bb_command_push(&bb_sound.commands,&c)){bb_say("KEYS BUSY / RETRY");return;}
        bb_keys_pitch[owner][k]=c.voice;bb_keys_sent[owner]|=1u<<k;bb_dirty=1;
    }
}
static int bb_band_available(unsigned channel) {
    return channel==BB_MIX_DRUMS || (channel==BB_MIX_DILLO && BB_WITH_DILLO) ||
        (channel==BB_MIX_TELEGRAM && BB_WITH_TELEGRAM);
}
static void bb_select_instrument(int delta) {
    bb_sound_pick_page=bb_sound_pick_return=0;
    bb_aux_cancel(1);
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    unsigned row=0;
    (void)st_from_editor(bb_face.requested.page,
        bb_face.requested.page==BB_DRUMS?bb_face.machine:0,&row);
    (void)st_select(&row,delta);
    st_route route;
    if(st_resolve(row,&route)) {
        bb_face.requested.page=route.page;
        if(route.page==BB_DRUMS)bb_face.machine=(uint8_t)route.target;
    }
#else
    static const bb_page pages[]={BB_DRUMS,
#if BB_WITH_DILLO
        BB_DILLO,
#endif
#if BB_WITH_TELEGRAM
        BB_TELEGRAM,
#endif
    };
    int slot=0,count=(int)(sizeof pages/sizeof pages[0]);
    for(int i=0;i<count;i++)if(pages[i]==bb_face.requested.page)slot=i;
    slot=bb_limit(slot+bb_limit(delta,-3,3),0,count-1);
    bb_face.requested.page=pages[slot];
#endif
#if BB_WITH_BROWSER
    bb_library_page=0;
#endif
    bb_pad_page=1;bb_keys_page[0]=bb_keys_page[1]=1;
    bb_face.bank=0;bb_mix_page=0;bb_diag_page=0;bb_tone_page=0;bb_drum_sound_page=0;bb_drum_color_page=0;bb_kit_page=0;bb_dirty=1;
#if BB_WITH_EFFECTS
    bb_send_page=bb_effect_page=0;
#endif
}
static void bb_select_layer(int delta) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    bb_face.machine=(uint8_t)bb_limit((int)bb_face.machine+bb_limit(delta,-1,1),0,1);
    bb_dirty=1;
#else
    (void)delta;bb_face.machine=0;bb_dirty=1;
#endif
}
static void bb_add_layer(void) {
    bb_face.machine=0;bb_kit_page=0;bb_pad_page=1;bb_dirty=1;
    bb_say("DRUM PADS / FOUR GROUP LEVELS");
}
static unsigned bb_drum_knob_parameter(unsigned track,unsigned knob) {
    static const uint8_t basic[4]={BB_DRUM_TUNE,BB_DRUM_DECAY,BB_DRUM_LEVEL,BB_DRUM_DRIVE};
    static const uint8_t bass[4]={BB_DRUM_TONE,BB_DRUM_ATTACK,BB_DRUM_DIST,BB_DRUM_LEVEL};
    static const uint8_t snare[4]={BB_DRUM_TONE,BB_DRUM_SNAPPY,BB_DRUM_DIST,BB_DRUM_LEVEL};
    static const uint8_t tom[4]={BB_DRUM_MODE,BB_DRUM_DRIVE,BB_DRUM_DIST,BB_DRUM_LEVEL};
    static const uint8_t clap[4]={BB_DRUM_MODE,BB_DRUM_ATTACK,BB_DRUM_DIST,BB_DRUM_DRIVE};
    static const uint8_t metal[4]={BB_DRUM_DIST,BB_DRUM_TUNE,BB_DRUM_DECAY,BB_DRUM_DRIVE};
    return !bb_drum_color_page?basic[knob]:track==0?bass[knob]:track==1?snare[knob]:track==6?clap[knob]:track<=5?tom[knob]:metal[knob];
}
static int bb_send_drum_parameter(unsigned parameter,unsigned value) {
    if(bb_kit_family[bb_face.machine]==1){bb_say("909: OPEN SOUND PAGE");return 0;}
    bb_command c={0};c.type=BB_CMD_DRUM_PARAMETER;c.target=bb_face.machine;
    c.voice=bb_face.voice;c.step=(uint8_t)parameter;c.value=(uint16_t)value;bb_dirty=1;
#if BB_WITH_RICH_DRUM_BROWSER
    if(bb_source_tone_context()){
        const bb_source_tone_view *view=&bb_rich_tones[bb_face.machine];
        if(!view->valid||!view->supported){bb_say("SOURCE TONE NOT READY");return 0;}
        if(parameter==BB_DRUM_MODE){bb_say("SOURCE VOICE IS FIXED");return 0;}
        c.type=BB_CMD_DRUM_SOURCE_PARAMETER;c.voice=bb_rich_sounds[bb_rich_voices[bb_face.machine]];
        c.note.note=(uint8_t)view->epoch;c.note.gate=(uint8_t)(view->epoch>>8);
        c.note.accent=(uint8_t)(view->epoch>>16);c.note.slide=(uint8_t)(view->epoch>>24);
    }
#endif
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
#if BB_WITH_RICH_DRUM_BROWSER
    if(c.type==BB_CMD_DRUM_SOURCE_PARAMETER)bb_rich_tones[bb_face.machine].pending=1;
#endif
    return 1;
}
static int bb_send_synth(unsigned synth,bb_command_type type,unsigned value,unsigned parameter) {
    bb_command c={0};c.type=type;c.target=(uint8_t)synth;c.value=(uint16_t)value;c.voice=(uint8_t)parameter;
    bb_dirty=1;
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
    return 1;
}
#include "bb_knob_contract.inc"
#include "bb_mixer_contract.inc"
static unsigned bb_knob_parameter(unsigned synth,unsigned knob) {
    const bb_knob_descriptor *control=bb_synth_knob(synth,bb_tone_page,knob);
    return control?control->parameter:BB_UI_UNASSIGNED;
}
#if BB_WITH_EFFECTS
static unsigned bb_effect_knob_parameter(unsigned knob) {
    const bb_knob_descriptor *control=bb_fx_knob(bb_effect_bank,knob);
    return control?control->parameter:BB_UI_UNASSIGNED;
}
static int bb_send_effect_parameter(unsigned parameter,unsigned value) {
    bb_command c={0};c.type=BB_CMD_FX_PARAMETER;c.voice=(uint8_t)parameter;c.value=(uint16_t)value;bb_dirty=1;
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
    return 1;
}
#include "bb_sample_sends_input.inc"
static void bb_effect_input(void) {
    int32_t delta;unsigned k;
    if(bb_effect_page) {
        if((delta=bb_read_encoder(EN_PRESET))!=0){bb_effect_bank=(uint8_t)bb_limit((int)bb_effect_bank+bb_limit(delta,-3,3),0,3);bb_dirty=1;}
        for(k=0;k<4;k++)if((delta=bb_read_encoder(EN_K1+k))!=0) {
            unsigned p=bb_effect_knob_parameter(k),lo,hi;
            if(p==BB_UI_UNASSIGNED)continue;
            bb_fx_parameter_limits(p,&lo,&hi);
            int scale=p==BB_FX_ECHO_MS?5:1;
            unsigned value=(unsigned)bb_limit((int)bb_fx_parameter_get(&bb_face.requested,p)+bb_limit(delta,-1000,1000)*scale,(int)lo,(int)hi);
            bb_send_effect_parameter(p,value);
        }
    } else if(bb_face.requested.page==BB_DRUMS) {
#if BB_SAMPLE_HOST_INTEGRATION
        if(bb_sample_sends_input())return;
#endif
        static const uint8_t parameters[4]={BB_DRUM_ECHO,BB_DRUM_SPRING,BB_DRUM_LEVEL,BB_DRUM_DRIVE};
        if((delta=bb_read_encoder(EN_PRESET))!=0){
#if BB_WITH_RICH_DRUM_BROWSER
            if(bb_source_tone_context())bb_rich_move(bb_limit(delta,-17,17));else
#endif
            bb_face.voice=(uint8_t)bb_limit((int)bb_face.voice+bb_limit(delta,-11,11),0,10);bb_dirty=1;
        }
        for(k=0;k<4;k++)if((delta=bb_read_encoder(EN_K1+k))!=0) {
            unsigned p=parameters[k];
            bb_send_drum_parameter(p,(unsigned)bb_limit((int)bb_visible_drum_value(&bb_face.requested.drums[bb_face.machine].sound,p)+bb_limit(delta,-127,127),0,127));
        }
    } else {
        /* Only real synth send controls here. Do not duplicate tone controls
         * on the other two knobs or pretend a master synth fader exists. */
        unsigned base=bb_selected_synth()?BB_FX_TELEGRAM_ECHO:BB_FX_DILLO_ECHO;
        (void)bb_read_encoder(EN_PRESET);(void)bb_read_encoder(EN_K3);(void)bb_read_encoder(EN_K4);
        for(k=0;k<2;k++)if((delta=bb_read_encoder(EN_K1+k))!=0)
            bb_send_effect_parameter(base+k,(unsigned)bb_limit((int)bb_fx_parameter_get(&bb_face.requested,base+k)+bb_limit(delta,-127,127),0,127));
    }
}
static int bb_panel_transport(uint32_t pressed) {
    if(!(pressed&(1u<<panel.btn[B_PLAY])))return 0;
    /* Explicit transport outranks detents, notes and edits from the old page.
     * Shared by instrument, browser, mixer and effects/sends routes; preserve
     * the current page and bounded return history instead of editing on STOP. */
    for(unsigned encoder=0;encoder<7;encoder++)(void)bb_read_encoder(encoder);
    bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
    bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);
    return 1;
}
#endif
static void bb_pitch_edit(unsigned synth,int delta) {
    unsigned step=bb_selected_note[synth];
    if(bb_panel_synth_shift(&bb_face,&bb_sound.commands,synth,step,bb_limit(delta,-127,127))) {
        bb_entry_pitch[synth]=bb_face.requested.synths[synth].notes[step].note;bb_dirty=1;
    } else bb_say("BUSY - RETRY");
}
/* Identical sound controls on the instrument and its pattern browser. */
static void bb_synth_knobs_input(unsigned synth,int rec) {
    unsigned k;int32_t delta;
    bb_sequence *seq=&bb_face.requested.synths[synth];
    for(k=0;k<4;k++)if((delta=bb_read_encoder(EN_K1+k))!=0) {
        const bb_knob_descriptor *control=bb_synth_control(synth,bb_tone_page,k,rec);
        if(!control)continue;
        unsigned parameter=control->parameter;
        if(parameter==BB_UI_LENGTH) {
            if(bb_send_synth(synth,BB_CMD_SYNTH_LENGTH,(unsigned)bb_limit(seq->length+bb_limit(delta,-64,64),1,64),0)) {
                if(bb_face.bank*16u>=seq->length)bb_face.bank=(uint8_t)((seq->length-1u)/16u);
                if(bb_selected_note[synth]>=seq->length)bb_selected_note[synth]=(uint8_t)(seq->length-1u);
            }
        } else {
            unsigned lo,hi;
            if(parameter==BB_UI_LEVEL) {
                bb_send_machine(synth+1u,BB_CMD_BAND_LEVEL,(unsigned)bb_limit(
                    (int)bb_face.requested.band_level[synth+1u]+bb_limit(delta,-127,127),0,127));
                continue;
            }
            int scale=parameter==BB_PARAM_CUTOFF?50:parameter==BB_PARAM_RESONANCE?25:parameter==BB_PARAM_LFO_RATE?10:1;
            bb_synth_parameter_limits(synth,parameter,&lo,&hi);
            unsigned value=(unsigned)bb_limit((int)bb_synth_parameter_get(&bb_face.requested,synth,parameter)+bb_limit(delta,-20000,20000)*scale,(int)lo,(int)hi);
            if(bb_send_synth(synth,BB_CMD_SYNTH_PARAMETER,value,parameter))bb_patch_edited[synth]=1;
        }
    }
}
static void bb_synth_input(uint32_t pressed,uint32_t notes) {
    unsigned synth=bb_selected_synth(),k;int32_t delta;
    bb_sequence *seq=&bb_face.requested.synths[synth];
    int rec=(fm1_in.buttons&(1u<<panel.btn[B_REC]))!=0;
    if((delta=bb_read_encoder(EN_PRESET))!=0) {
        if(rec)bb_pitch_edit(synth,delta);
        else {
            unsigned index=(unsigned)bb_limit((int)bb_patch_index[synth]+bb_limit(delta,-8,8),0,
                (int)(synth?BB_TELEGRAM_PATCHES:BB_DILLO_PATCHES)-1); /* every bank slot, not just the first eight */
            if(bb_send_synth(synth,synth?BB_CMD_TELEGRAM_PATCH:BB_CMD_DILLO_PATCH,index,0)) {
                bb_patch_index[synth]=(uint8_t)index;bb_patch_edited[synth]=0;bb_patch_from_scene[synth]=0;
            }
        }
    }
    bb_synth_knobs_input(synth,rec);
    if(pressed&(1u<<panel.btn[B_EDIT])) {
        if(bb_keys_page[synth]){bb_keys_page[synth]=0;bb_face.requested.edit=BB_GATE;}
        else bb_face.requested.edit=(bb_edit)((bb_face.requested.edit+1u)%3u);
        bb_dirty=1;
    }
    if(pressed&(1u<<panel.btn[B_ARP])) {
        if(rec) {
            bb_keys_blocked|=fm1_in.notes;bb_keys_taps=0;
            bb_send_synth(synth,BB_CMD_SYNTH_MUTE,!seq->muted,0);
        } else {bb_keys_page[synth]=1;bb_say("KEYS / SEQ: WRITE NOTES");}
    }
    if(pressed&(1u<<panel.btn[B_ENV])){bb_tone_page=bb_tone_page==1?0:1;bb_dirty=1;}
    if((pressed&(1u<<panel.btn[B_LFO])) && !rec) {
        if(synth){bb_tone_page=bb_tone_page==2?0:2;bb_dirty=1;}
        else bb_say("DILLO LFO NOT BUILT");
    }
    if(pressed&(1u<<panel.btn[B_SCL])) {
        if(synth) {
            if(bb_send_synth(synth,BB_CMD_SYNTH_PARAMETER,!bb_face.requested.telegram.glide,BB_PARAM_GLIDE)) {
                bb_patch_edited[synth]=1;
                bb_say(bb_face.requested.telegram.glide?"LEAD: GLIDE ON":"LEAD: GLIDE OFF");
            }
        } else if(bb_send_synth(0,BB_CMD_SYNTH_PARAMETER,!bb_face.requested.dillo.wave,BB_PARAM_WAVE)) {
            bb_patch_edited[0]=1;
            bb_say(bb_face.requested.dillo.wave?"DILLO: SAW":"DILLO: SQUARE");
        }
    }
    if(bb_keys_page[synth]) {
        int oct=(pressed&(1u<<panel.btn[B_OCTUP])?1:0)-(pressed&(1u<<panel.btn[B_OCTDN])?1:0);
        if(oct) {
            int wanted=bb_limit((int)bb_pattern_shift[synth]+oct*12,-BB_TRANSPOSE_LIMIT,BB_TRANSPOSE_LIMIT);
            int base=bb_limit((int)bb_keys_base[synth]+wanted-bb_pattern_shift[synth],5,101);
            wanted=bb_pattern_shift[synth]+base-bb_keys_base[synth];
            if(wanted!=bb_pattern_shift[synth]) {
                bb_command c={0};c.type=BB_CMD_SYNTH_TRANSPOSE;c.target=(uint8_t)synth;
                c.value=(uint16_t)(wanted+BB_TRANSPOSE_LIMIT);
                if(bb_command_push(&bb_sound.commands,&c)) {
                    bb_keys_base[synth]=(uint8_t)base;bb_pattern_shift[synth]=(int8_t)wanted;
                    bb_say("OCTAVE: KEYS AND PATTERN");
                } else bb_say("BUSY - RETRY OCTAVE");
            } else bb_say("OCTAVE LIMIT");
        }
    } else {
        int pitch=(pressed&(1u<<panel.btn[B_OCTUP])?1:0)-(pressed&(1u<<panel.btn[B_OCTDN])?1:0);
        if(pitch)bb_pitch_edit(synth,pitch);
    }
    if(!bb_diag_page && !bb_keys_page[synth])for(k=0;k<27;k++)if(notes&(1u<<k)) {
        int local=bb_panel_white_step(k);
        if(local>=0 && bb_face.bank*16u+(unsigned)local<seq->length) {
            unsigned step=bb_face.bank*16u+(unsigned)local;
            bb_edit edit=rec?BB_ACCENT:bb_face.requested.edit;
            unsigned pitch=seq->notes[step].gate?seq->notes[step].note:bb_entry_pitch[synth];
            bb_selected_note[synth]=(uint8_t)step;
            if(!bb_panel_synth_step(&bb_face,&bb_sound.commands,synth,(unsigned)local,edit,pitch))bb_say("NOTE EDIT REJECTED");
            else bb_entry_pitch[synth]=(uint8_t)pitch;
            bb_dirty=1;
        }
    }
}
#if BB_WITH_SESSION
static void bb_scene_mirror_reset(void) {
    /* A restored scene supersedes any already-consumed sound-picker ticket.
     * The scene request waits for the command queue and kit switch to settle;
     * retaining its UI pending bit would block the next PRESETS gesture. */
    bb_sound_pick_page=bb_sound_pick_return=0;
    memset(&bb_sound_pick,0,sizeof bb_sound_pick);
    bb_sound_pick_view=0;
#if BB_WITH_BROWSER
    bb_library_reset();
#endif
    bb_panel_init(&bb_face,&bb_sound.core);
    bb_mix_page=bb_diag_page=bb_tone_page=bb_drum_sound_page=bb_drum_color_page=bb_kit_page=0;
    bb_patch_index[0]=bb_patch_index[1]=0;bb_patch_edited[0]=bb_patch_edited[1]=1;
    bb_patch_from_scene[0]=bb_patch_from_scene[1]=1;
    bb_selected_note[0]=bb_selected_note[1]=0;bb_entry_pitch[0]=36;bb_entry_pitch[1]=60;
    bb_pad_page=1;bb_scope_enabled=0;
    memset(bb_pattern_shift,0,sizeof bb_pattern_shift);bb_keys_base[0]=41;bb_keys_base[1]=53;
#if BB_WITH_EVOLVE
    bb_evo_page=0;bb_evo_sync_pending=0;memset(&bb_evo_view,0,sizeof bb_evo_view);
#endif
#if BB_WITH_EFFECTS
    bb_send_page=bb_effect_page=0;
#endif
    bb_dirty=1;
}
static int bb_scene_input(uint32_t pressed,uint32_t notes,int rec) {
#if BB_WITH_BROWSER
    if(pressed&(1u<<panel.btn[B_SAVE]))bb_library_page=0;
#endif
    int32_t select;
    if(bb_session_poll()) {
        if(bb_session_loaded){bb_scene_mirror_reset();bb_session_loaded=0;}
        bb_say(bb_session_message);
    }
    if(!bb_scene_page && !(pressed&(1u<<panel.btn[B_SAVE])) && !bb_session_busy())return 0;
    /* STOP wins over scene entry and storage-busy UI. Do not start playback
     * or process a simultaneous SAVE from this dialog. */
    if(pressed&(1u<<panel.btn[B_PLAY])) {
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        if(bb_face.requested.running)bb_send(BB_CMD_TRANSPORT,0);
        else bb_say("HOME: RETURN TO PLAY");
        return 1;
    }
    select=bb_read_encoder(EN_PRESET);
    (void)bb_read_encoder(EN_SELECT);(void)bb_read_encoder(EN_ALGO);
    for(unsigned i=0;i<4;i++)(void)bb_read_encoder(EN_K1+i);
    if(bb_session_busy())return 1;
    if(!bb_scene_page){bb_scene_page=1;bb_say("SAVE: STORE / REC+SAVE: LOAD");return 1;}
    if(pressed&((1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_SEQ]))) {
        bb_scene_page=0;bb_dirty=1;return 1;
    }
    if(select){bb_scene_slot=(uint8_t)bb_limit((int)bb_scene_slot+bb_limit(select,-3,3),0,3);bb_dirty=1;}
    for(unsigned key=0;key<27;key++)if(notes&(1u<<key)) {
        int index=bb_panel_white_step(key);
        if(index>=0 && index<4){bb_scene_slot=(uint8_t)index;bb_dirty=1;}
    }
    if(pressed&(1u<<panel.btn[B_SAVE])) {
        (void)bb_session_request(bb_scene_slot,rec);bb_say(bb_session_message);
    }
    return 1;
}
#endif
#if BB_WITH_EVOLVE
static void bb_evo_sync(void) {
    bb_commands *q=&bb_sound.commands;bb_evo_snapshot view;
    memset(&view,0,sizeof view);
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    if(!bb_commands_idle(q)){if(q->leave)q->leave(q->lock_context,token);return;}
    view.settings=bb_sound.evolve.settings;view.changes=bb_sound.evolve.changes;
    view.ready=bb_sound.evolve.ready;view.enabled=bb_sound.evolve.enabled;
    view.frozen=bb_sound.evolve.frozen;view.conflict=bb_sound.evolve.conflict;
    view.pending=bb_sound.evolve_home_pending;view.result=bb_sound.evolve_result;
    if(q->leave)q->leave(q->lock_context,token);
    if(memcmp(&view,&bb_evo_view,sizeof view)){
        bb_evo_view=view;bb_dirty=1;bb_evo_sync_pending=1;
    }
    if(!view.ready){bb_evo_sync_pending=0;return;}
    /* Stable Evolution metadata cannot introduce another pattern mutation.
     * Avoid a full guarded pattern copy on every input poll. Keep a deferred
     * copy until the complete queue/STOP predicate permits synchronization. */
    if(bb_evo_sync_pending){
        int synced=bb_panel_sync_patterns(&bb_face,q,&bb_sound.core);
        if(synced){bb_evo_sync_pending=0;if(synced==2)bb_dirty=1;}
    }
}
static int bb_evo_source_blocked(void) {
#if BB_WITH_RICH_DRUM_RENDER
    /* Match current audio admission before updating optimistic controls.
     * A rich phrase may belong to the other drum, not the focused row. */
    bb_commands *q=&bb_sound.commands;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    int unavailable=bb_sound.rich.valid[0]||bb_sound.rich.valid[1];
    if(q->leave)q->leave(q->lock_context,token);
    return unavailable;
#else
    return 0;
#endif
}
static int bb_evo_send(unsigned action,unsigned value,unsigned part,unsigned step) {
    if(bb_evo_source_blocked()){bb_say("SOURCE PATTERN: EVOLVE OFF");return 0;}
    bb_command c={0};c.type=BB_CMD_EVOLVE;c.voice=(uint8_t)action;
    c.value=(uint16_t)value;c.target=(uint8_t)part;c.step=(uint8_t)step;
    if(!bb_panel_send(&bb_face,&bb_sound.commands,&c)){bb_say("BUSY - RETRY");return 0;}
    /* Optimistic controls only. The consumer's authoritative settings and
     * result replace these once the FIFO drains; patterns are never copied back. */
    if(action==BB_EV_AMOUNT)bb_evo_view.settings.amount=(uint8_t)value;
    if(action==BB_EV_INTERVAL)bb_evo_view.settings.every_phrases=(uint8_t)value;
    if(action==BB_EV_ROOT)bb_evo_view.settings.root=(uint8_t)value;
    if(action==BB_EV_SCALE)bb_evo_view.settings.scale=(uint16_t)value;
    if(action==BB_EV_PARTS)bb_evo_view.settings.parts=(uint8_t)value;
    if(action==BB_EV_ATTRIBUTES)bb_evo_view.settings.attributes=(uint8_t)value;
    if(action==BB_EV_LOCK) {
        if(value)bb_evo_view.settings.locked[part]|=UINT64_C(1)<<step;
        else bb_evo_view.settings.locked[part]&=~(UINT64_C(1)<<step);
    }
    /* Capture/Home can reset to metadata already displayed. An accepted
     * request still needs one authoritative mirror refresh after it drains. */
    bb_evo_sync_pending=1;bb_dirty=1;return 1;
}
#define BB_EVOLUTION_PANEL_PRIORITY 1
static int bb_evo_input(uint32_t pressed,uint32_t notes,int rec) {
    int32_t delta;
    /* Explicit physical transport wins, including missing/rejected capture
     * and a same-scan opening chord. STOP uses the existing priority latch;
     * no queue-full fallback, audio reset or remote playback is introduced. */
    if((bb_evo_page || (rec&&(pressed&(1u<<panel.btn[B_EDIT])))) &&
       (pressed&(1u<<panel.btn[B_PLAY]))) {
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
        bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
    }
    if(rec && (pressed&(1u<<panel.btn[B_EDIT]))) {
        if(!bb_evo_page&&bb_evo_source_blocked()) {
            for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
            bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
            bb_say("SOURCE PATTERN: EVOLVE OFF");return 1;
        }
        bb_evo_page^=1u;bb_dirty=1;
        if(bb_evo_page && !bb_evo_view.ready)bb_evo_send(BB_EV_CAPTURE,0,0,0);
        bb_say(bb_evo_page?"EVOLVE / HOME: BACK":"BACK TO INSTRUMENT");
        /* Consume the opening gesture, not EDIT's normal articulation action. */
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        return 1;
    }
    if(!bb_evo_page)return 0;
    if(bb_evo_source_blocked()) {
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=bb_keys_taps=0;
        bb_evo_page=0;bb_dirty=1;
        bb_say("SOURCE PATTERN: EVOLVE OFF");return 1;
    }
    if(bb_evo_rec!=(uint8_t)rec || pressed || notes)bb_dirty=1;
    bb_evo_rec=(uint8_t)rec;
    (void)bb_read_encoder(EN_PRESET);(void)bb_read_encoder(EN_ALGO);(void)bb_read_encoder(EN_SELECT);
    if(!bb_evo_view.ready) {
        for(unsigned k=0;k<4;k++)(void)bb_read_encoder(EN_K1+k);
        if(pressed&(1u<<panel.btn[B_HOME])){bb_evo_page=0;bb_dirty=1;}
        return 1;
    }
    if((delta=bb_read_encoder(EN_K1)))bb_evo_send(BB_EV_AMOUNT,(unsigned)bb_limit((int)bb_evo_view.settings.amount+bb_limit(delta,-100,100),0,100),0,0);
    if((delta=bb_read_encoder(EN_K2)))bb_evo_send(BB_EV_INTERVAL,(unsigned)bb_limit((int)bb_evo_view.settings.every_phrases+bb_limit(delta,-64,64),1,64),0,0);
    if((delta=bb_read_encoder(EN_K3)))bb_evo_send(BB_EV_ROOT,(unsigned)bb_limit((int)bb_evo_view.settings.root+bb_limit(delta,-12,12),0,11),0,0);
    if((delta=bb_read_encoder(EN_K4)))bb_evo_send(BB_EV_SCALE,delta>0?0x5ad:0xab5,0,0);
    if(pressed&(1u<<panel.btn[B_PLAY]))bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);
    if(pressed&(1u<<panel.btn[B_ARP])) {
        if(bb_evo_view.enabled && !bb_evo_view.frozen)bb_evo_send(BB_EV_ENABLE,0,0,0);
        else bb_evo_send(BB_EV_RUN,0,0,0);
    }
    if(pressed&(1u<<panel.btn[B_EDIT]))bb_evo_send(BB_EV_FREEZE,bb_evo_view.pending?1u:!bb_evo_view.frozen,0,0);
    if(pressed&(1u<<panel.btn[B_ENV]))bb_evo_send(BB_EV_CAPTURE,0,0,0);
    if(pressed&(1u<<panel.btn[B_HOME])) {
        if(rec)bb_evo_send(BB_EV_HOME,0,0,0);
        else {bb_evo_page=0;bb_dirty=1;}
    }
    if(pressed&(1u<<panel.btn[B_SEQ])){bb_evo_page=0;bb_dirty=1;}
    for(unsigned k=0;k<27;k++)if(notes&(1u<<k)) {
        int key=bb_panel_white_step(k);if(key<0)continue;
        if(rec) {
            unsigned part=bb_face.requested.page==BB_DRUMS?bb_face.machine:BB_MACHINES+bb_selected_synth();
            unsigned step=bb_face.bank*16u+(unsigned)key;
            if(step<BB_STEPS)bb_evo_send(BB_EV_LOCK,!(bb_evo_view.settings.locked[part]&(UINT64_C(1)<<step)),part,step);
        } else if(key<6)bb_evo_send(BB_EV_PARTS,bb_evo_view.settings.parts^(1u<<key),0,0);
        else if(key<10)bb_evo_send(BB_EV_ATTRIBUTES,bb_evo_view.settings.attributes^(1u<<(key-6)),0,0);
    }
    return 1;
}
#endif
#if BB_WITH_BROWSER
static bb_player *bb_library_current(void) {
    return bb_face.requested.page==BB_DRUMS?&bb_drum_library[bb_face.machine]:&bb_library[bb_selected_synth()];
}
static const char *bb_library_error_text(int result) {
    return result==BB_FACTORY_PATTERN_OK?"PATTERN REQUEST ACCEPTED":
           result==BB_PLAYER_NEEDS_KIT?"KIT VOICES DO NOT MATCH":
           result==BB_PLAYER_NEEDS_TIMING?"SOURCE TIMING NOT YET SUPPORTED":
           result==BB_PLAYER_NEEDS_VOICE?"SOURCE VOICE NOT YET SUPPORTED":
           result==BB_FACTORY_PATTERN_EXPRESSION?"EXPRESSION NOT YET SUPPORTED":
           result==BB_FACTORY_PATTERN_BUSY?"BUSY / SELECT AGAIN":
           result==BB_PLAYER_CANCELLED?bb_notice_text(BB_NOTICE_CANCELLED_BY_STOP):
           result==BB_PLAYER_AUDIO_FAULT?bb_notice_text(BB_NOTICE_REQUEST_FAULT):"PATTERN UNAVAILABLE";
}
static void bb_library_result(int result){bb_say(bb_library_error_text(result));}
static void bb_library_sync(void) {
    bb_rich_refresh();
    for(unsigned i=0;i<BB_SYNTHS+BB_MACHINES;i++) {
        bb_player *p=i<BB_SYNTHS?&bb_library[i]:&bb_drum_library[i-BB_SYNTHS];
        if(!p->ready)continue;
        bb_pattern_status previous=p->status;
        if(bb_player_sync(p,&bb_sound) && memcmp(&previous,&p->status,sizeof previous)) {
            bb_dirty=1;
            unsigned part=i<BB_SYNTHS?BB_MACHINES+i:i-BB_SYNTHS;
            bb_library_panel_pending|=(uint8_t)(1u<<part);
            if(p==bb_library_current() && p->request.ticket && p->status.seen==p->request.ticket &&
               p->status.result==BB_PATTERN_REJECTED && p->error)
                bb_library_result(p->error);
            /* D4: STOP ended this queued kit+pattern. Say so once. */
            if(p->request.ticket && p->status.seen==p->request.ticket &&
               p->status.result==BB_PATTERN_CANCELLED && p->error==BB_PLAYER_CANCELLED)
                bb_say(bb_notice_text(BB_NOTICE_CANCELLED_BY_STOP));
            /* D1/D4/#31: the request that carried the chosen kit has ended
             * (applied, refused, faulted or cancelled). Drop the intent so the
             * screen names the kit the audio owner reports, never a stale NEXT KIT. */
            if(p->is_drum && p->target<BB_MACHINES &&
               bb_kit_intent_settled(p,bb_kit_intent[p->target].valid,bb_kit_intent[p->target].id))
                bb_kit_intent[p->target].valid=0;
        }
    }
    /* Coalesce status notifications without losing a resync when commands
     * are queued. The existing guarded copy remains the sole snapshot owner. */
    if(bb_library_panel_pending &&
       bb_panel_sync_patterns_mask(&bb_face,&bb_sound.commands,&bb_sound.core,bb_library_panel_pending)) {
        bb_library_panel_pending=0;bb_dirty=1;
    }
}
#include "bb_player_preview.h"
/* Isolated kit-first trial: one explicit preview/confirm policy. The old
 * turn-to-load branch remains in the source but is not linked into this
 * resource probe. This is not a persistent user setting. */
#define bb_pattern_confirm_first 1
/* The chosen kit is a UI intent until the exact kit+phrase pair commits. Use
 * that intent for preview eligibility; otherwise consult the audio owner's
 * current kit. None of these helpers changes audio or queued requests. */
static int bb_library_preview_kit(const bb_player *player,unsigned *kit) {
#if BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_HYBRID_TRIAL)
    if(!player||!kit||!player->is_drum||player->target>=BB_MACHINES)return 0;
    if(bb_kit_intent[player->target].valid){*kit=bb_kit_intent[player->target].id;return 1;}
    bb_sample_status status;
    if(bb_audio_sample_status_layer(&bb_sound,player->target,&status)&&status.count){
        *kit=status.selected;return 1;
    }
#else
    (void)player;(void)kit;
#endif
    return 0;
}
static int bb_library_preview_move(bb_player *player,int delta) {
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    unsigned kit;
    return bb_library_preview_kit(player,&kit)?
        bb_player_preview_move_kit(player,&bb_sound,kit,delta):bb_player_preview_move(player,delta);
#else
    return bb_player_preview_move(player,delta);
#endif
}
static int bb_library_preview_bank_key(bb_player *player,unsigned key) {
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    unsigned kit;
    return bb_library_preview_kit(player,&kit)?
        bb_player_preview_bank_key_kit(player,&bb_sound,kit,key):bb_player_preview_bank_key(player,key);
#else
    return bb_player_preview_bank_key(player,key);
#endif
}
static int bb_library_preview_part_key(bb_player *player,unsigned key) {
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    unsigned kit;
    return bb_library_preview_kit(player,&kit)?
        bb_player_preview_part_key_kit(player,&bb_sound,kit,key):bb_player_preview_part_key(player,key);
#else
    return bb_player_preview_part_key(player,key);
#endif
}
static int bb_library_preview_group(bb_player *player,unsigned group) {
    if(player->is_drum && group==BB_BROWSER_ACID)return 0;   /* ACID holds Dillo phrases only */
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    unsigned kit;
    return bb_library_preview_kit(player,&kit)?
        bb_player_preview_group_kit(player,&bb_sound,kit,group):bb_player_group(player,group);
#else
    return bb_player_group(player,group);
#endif
}
static int bb_library_confirm(bb_player *player) {
    if(bb_face.requested.page==BB_DRUMS && bb_face.machine<BB_MACHINES &&
       bb_kit_intent[bb_face.machine].valid)
        return bb_player_confirm_with_kit(player,&bb_sound,bb_kit_intent[bb_face.machine].id);
    return bb_player_confirm(player,&bb_sound);
}
static int bb_library_input(uint32_t pressed,uint32_t *notes,int rec) {
    if(bb_mix_page || bb_diag_page || bb_effect_page || bb_send_page)return 0;
    int drums=bb_face.requested.page==BB_DRUMS;
    /* Transport wins over browsing, even if an encoder detent or key arrives
     * in the same scan. No phrase is submitted by that gesture. */
    if(bb_library_page && (pressed&(1u<<panel.btn[B_PLAY]))) {
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        if(!bb_face.requested.running && drums && bb_face.machine<BB_MACHINES &&
           bb_kit_intent[bb_face.machine].valid) {
            /* First PLAY is the visible kit+phrase commit, followed by START.
             * Reserve both FIFO entries before the potentially costly phrase
             * preflight. An invalid pair must remain stopped and unchanged. */
            uintptr_t token=bb_target_enter(0);
            int space=bb_sound.commands.count+2u<=BB_COMMAND_CAPACITY &&
                      !bb_sound.commands.stop_pending && !bb_sound.commands.stop_inflight;
            bb_target_leave(0,token);
            if(!space){bb_say("BUSY / SELECT AGAIN");return 1;}
            int result=bb_library_confirm(bb_library_current());
            if(result!=BB_FACTORY_PATTERN_OK){bb_library_result(result);return 1;}
        }
        bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
    }
    if(pressed&(1u<<panel.btn[B_ENV])) {
        /* ENV must reach the native sound/voice editor below this browser.
         * ALGORITHM already owns patterns. Navigation consumes old-page
         * detents and notes, never an accidental parameter or pattern edit. */
        for(unsigned encoder=0;encoder<7;encoder++)(void)bb_read_encoder(encoder);
        bb_keys_blocked|=fm1_in.notes;bb_keys_edges=0;*notes=0;
        if(pressed&(1u<<panel.btn[B_PLAY])) {
            bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);return 1;
        }
        bb_library_page=0;
        return 0;
    }
    /* The same primary knob reaches patterns from every playing face.
     * Detailed kit/voice editors keep their explicitly labelled roles.
     * Consume an unavailable-library turn; never reinterpret it as a
     * voice edit. Entering with held keys requires a lift before play. */
    int32_t pattern_delta=0;
    if(bb_library_page || (!rec && (!bb_kit_page || bb_sound_picker_enabled) && !bb_drum_sound_page))
        pattern_delta=bb_read_encoder(EN_ALGO);
    if(pattern_delta && !bb_library_page) {
        if(!bb_library_current()->ready){bb_say("LIBRARY NOT READY");return 1;}
        bb_library_page=1;bb_tone_page=0;bb_keys_blocked|=fm1_in.notes;
        bb_keys_edges=0;*notes=0;bb_dirty=1;
    }
    if(!bb_library_page)return 0;
    if(pressed&((1u<<panel.btn[B_HOME])|(1u<<panel.btn[B_SEQ])|
                (1u<<panel.btn[B_FX])|(1u<<panel.btn[B_GLO])|(1u<<panel.btn[B_LFO])|
                (1u<<panel.btn[B_ARP])|((drums && (!bb_pattern_confirm_first || rec))?(1u<<panel.btn[B_SCL]):0)) ||
       (rec && (pressed&(1u<<panel.btn[B_ENV])))) {
        bb_library_page=0;*notes=0;bb_keys_edges=0;bb_keys_blocked|=fm1_in.notes;
        return 0;
    }
    unsigned synth=bb_selected_synth();bb_player *player=bb_library_current();int32_t delta;
    int selection_changed=pattern_delta!=0;
    if(pattern_delta) {
        if(drums && rec){bb_select_layer(pattern_delta);player=bb_library_current();}
        else if(bb_pattern_confirm_first) {
            if(bb_library_preview_move(player,pattern_delta))bb_dirty=1;
            else bb_library_result(BB_FACTORY_PATTERN_INVALID);
        } else bb_library_result(bb_player_move(player,&bb_sound,pattern_delta));
    }
    /* Plain PRESETS is owned by the sound picker before this handler.
     * REC+PRESETS explicitly selects the pattern family without launching. */
    if((!bb_sound_picker_enabled||rec) && (delta=bb_read_encoder(EN_PRESET))!=0) {
        unsigned group=(unsigned)bb_limit((int)player->view.group+bb_limit(delta,-1,1),0,BB_BROWSER_GROUPS-1);
        (void)bb_library_preview_group(player,group);   /* a refused group (ACID on a drum layer, or none) keeps the current one */
        bb_dirty=1;selection_changed=1;
    }
    if(!drums)bb_synth_knobs_input(synth,rec);
    else {
    if((delta=bb_read_encoder(EN_K1))!=0)
        bb_send(BB_CMD_DRUM_LEVEL,(unsigned)bb_limit((int)bb_face.requested.drums[bb_face.machine].level+bb_limit(delta,-127,127),0,127));
    /* No accumulated hidden parameter edits when leaving this key mode. */
    for(unsigned k=1;k<4;k++)if((delta=bb_read_encoder(EN_K1+k))!=0 && drums) {
        if(k==3)bb_send(BB_CMD_SWING_X2,(unsigned)bb_limit((int)bb_swing_x2(bb_face.requested.swing)+bb_limit(delta,-50,50),100,150));
        else {
            unsigned parameter=k==1?BB_DRUM_TUNE:BB_DRUM_DECAY;
            const bb_drum_sound *sound=&bb_face.requested.drums[bb_face.machine].sound;
            bb_send_drum_parameter(parameter,(unsigned)bb_limit((int)bb_visible_drum_value(sound,parameter)+bb_limit(delta,-127,127),0,127));
        }
    }
    }
    int page=(pressed&(1u<<panel.btn[B_OCTUP])?1:0)-(pressed&(1u<<panel.btn[B_OCTDN])?1:0);
    if(page){(void)bb_player_page(player,page,rec);bb_dirty=1;selection_changed=1;}
    if(bb_pattern_confirm_first) {
        /* Keys remain direct musical actions. One input scan submits at most
         * one choice: white bank, then black section if both were pressed.
         * Held keys from another page must be lifted before they can select. */
        uint32_t fresh=*notes&~bb_keys_blocked;
        int chosen=0,valid=1;
        for(unsigned k=0;k<27;k++)if(fresh&(1u<<k)) {
            int key=bb_panel_white_step(k);
            if(key>=0){chosen=1;valid=bb_library_preview_bank_key(player,(unsigned)key);break;}
        }
        for(unsigned k=0;k<27;k++)if(fresh&(1u<<k)) {
            int key=bb_panel_black_index(k);
            if(key>=0 && key<8){chosen=1;valid=bb_library_preview_part_key(player,(unsigned)key)&&valid;break;}
        }
        if(chosen)bb_library_result(valid?bb_library_confirm(player):BB_FACTORY_PATTERN_INVALID);
        else if(!rec && !selection_changed && !*notes && (pressed&(1u<<panel.btn[B_SCL])))
            bb_library_result(bb_library_confirm(player));
        /* A simultaneous turn/page/family move plus SEL is preview-only;
         * release and press SEL again to commit the visible choice. */
    } else {
    for(unsigned k=0;k<27;k++)if(*notes&(1u<<k)) {
        int key=bb_panel_white_step(k);
        if(key>=0){bb_library_result(bb_player_bank_key(player,&bb_sound,(unsigned)key));break;}
    }
    for(unsigned k=0;k<27;k++)if(*notes&(1u<<k)) {
        int key=bb_panel_black_index(k);
        if(key>=0 && key<8){bb_library_result(bb_player_part_key(player,&bb_sound,(unsigned)key));break;}
    }
    }
    if(pressed&(1u<<panel.btn[B_PLAY]))bb_send(BB_CMD_TRANSPORT,!bb_face.requested.running);
    if(pressed&(1u<<panel.btn[B_EDIT])) {
        if(drums)bb_send(BB_CMD_DRUM_MUTE,!bb_face.requested.drums[bb_face.machine].muted);
        else bb_send_synth(synth,BB_CMD_SYNTH_MUTE,!bb_face.requested.synths[synth].muted,0);
    }
    bb_keys_edges=0;return 1; /* selection keys never become live notes */
}
#endif
#if BB_WITH_BROWSER
#include "bb_home_input.inc"
#endif
#include "bb_sound_picker_input.inc"
#include "bb_home_sound_browse.inc"
#include "bb_kit_first_entry.inc"
#include "bb_aux_browse.inc"
#include "home_master/input.inc"
#include "bb_design_help.inc"
#include "bb_nav_firmware.inc"
#include "bb_midi_ui_input.inc"
/* Navigation v2.1: the page-flag input path below is replaced by bb_nav
 * (bb_nav_firmware.inc). The former handlers it called (bb_aux_input,
 * hm_native_input, bb_aux_browse_input, bb_sound_pick_input,
 * bb_library_input, bb_scene_input, bb_evo_input, bb_synth_input,
 * bb_effect_input, bb_mixer_input, bb_group_*_input, bb_design_help_input)
 * remain in the source, unreferenced, for the merge review; the compiler
 * drops them. Their musical calls are reached from bb_nav's routes. */
static void bb_target_input_impl(void) {
    uint32_t pressed=fm1_input_edges(0),notes=fm1_input_note_edges();
    if(bb_midi_global_settings_holding()){
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        bb_nav_cancel(&bb_nav);bb_nav_project();return;
    }
    if(pressed || notes)fm1_led_anim_stop();
#if BB_WITH_SESSION
    if(bb_session_poll()) {
        if(bb_session_loaded) {
            /* A restored scene supersedes the browsers; land on STUDIO. */
            bb_scene_mirror_reset();bb_session_loaded=0;
            bb_nav_row=0;bb_nav_force(BB_NAV_STUDIO);
        }
        bb_say(bb_session_message);
    }
    if(bb_session_busy()) {
        /* Storage hold, as bb_scene_input: STOP still wins, nothing else. */
        for(unsigned k=0;k<7;k++)(void)bb_read_encoder(k);
        if((pressed&(1u<<panel.btn[B_PLAY]))&&bb_face.requested.running){bb_nav_notes_all_off();bb_send(BB_CMD_TRANSPORT,0);}
        bb_nav_cancel(&bb_nav);bb_nav_project();
        return;
    }
#endif
#if BB_WITH_EVOLVE
    bb_evo_sync();
#endif
    if(bb_midi_ui_input(pressed,notes))return;
    bb_nav_scan(pressed,notes);
}
/* A quiet REC hold is read-only help. Any musical gesture suppresses help
 * until release; it is never delayed, swallowed or replayed by the guide. */
/* Reliability r1: the audio owner may stop the transport by itself (D2
 * render fault). Once its STOP has been applied, resynchronise the UI's
 * transport mirror (PLAY LED, PLAY/STOP labels) and say why, once. */
static void bb_audio_fault_poll(void) {
    static uint32_t seen_render_stops;
    static uint8_t worker_notice;
    if(bb_worker_fault_drained) {
        /* D1: the transport stays stopped until reboot; a refused START must
         * not leave the PLAY LED lit. Resync the mirror on every poll. */
        uintptr_t token=bb_target_enter(0);
        int settled=bb_commands_idle(&bb_sound.commands);
        unsigned running=bb_sound.core.running;
        bb_target_leave(0,token);
        if(settled) {
            if(bb_face.requested.running!=running){bb_face.requested.running=(uint8_t)running;bb_dirty=1;}
            if(!worker_notice){worker_notice=1;bb_say(bb_notice_text(BB_NOTICE_WORKER_FAULT_STOP));}
        }
    }
    uint32_t stops=bb_render_fault_stops;
    if(stops==seen_render_stops)return;
    uintptr_t token=bb_target_enter(0);
    int settled=!bb_sound.commands.stop_pending && !bb_sound.commands.stop_inflight;
    unsigned running=bb_sound.core.running;
    bb_target_leave(0,token);
    if(!settled)return;
    seen_render_stops=stops;
    bb_face.requested.running=(uint8_t)running;bb_dirty=1;
    bb_say(bb_notice_text(BB_NOTICE_RENDER_FAULT_STOP));
}
static void bb_target_input(void) {
    if(bb_asset_hold_request){bb_help_cancel(&bb_design_help);bb_help_visible=0;bb_aux_cancel(1);return;}
    unsigned visible=bb_help_visible;
    bb_help_encoder=0;
    if(bb_kit_switch_poll()) {
        uintptr_t token=bb_target_enter(0);
        bb_face.requested=bb_sound.core;
        bb_target_leave(0,token);
        bb_dirty=1;bb_say(bb_kit_switch_message);
    }
    bb_sound_pick_poll();
    if(bb_kit_switch_busy()) {
        bb_help_cancel(&bb_design_help);bb_help_visible=0;
        bb_aux_cancel(1);bb_nav_cancel(&bb_nav);bb_nav_project();
        for(unsigned role=0;role<7u;role++)(void)bb_read_encoder(role);
        (void)fm1_input_edges(0);(void)fm1_input_note_edges();
        return; /* no producer can enqueue into the acknowledged pause */
    }
#if BB_WITH_BROWSER
    bb_library_sync();
    bb_home_poll();
#endif
    bb_audio_fault_poll(); /* after library sync: the fault notice wins the hint line */
    bb_nav_notes_retry(); /* owed note-offs take priority over further knob edits */
    bb_nav_throw_retry();
    bb_nav_arm_poll();   /* switch an off drum layer on only once its confirmed choice is applied */
#if BB_MIDI_INPUT
    {
        uint32_t both=(1u<<panel.btn[B_OCTDN])|(1u<<panel.btn[B_OCTUP]);
        int paused=0;
#if BB_WITH_SESSION
        paused=bb_session_busy()||bb_midi_global_settings_holding();
#endif
        bb_midi_input(paused,ui.uboot || (fm1_in.buttons&both)==both);
    }
#endif
    bb_target_input_impl();

    if(visible!=bb_help_visible)bb_dirty=1;
}
#define BB_DARK_FACE 1
#define BB_BG RGB(8,11,13)
#define BB_ART RGB(51,47,39)
#define BB_TEXT RGB(240,221,175)
#define BB_ORANGE RGB(255,175,68)
#define BB_DIM RGB(205,200,188)
#define BB_GREEN RGB(123,202,159)
#define BB_TELEGRAM_INK RGB(85,217,208)
#define BB_ROTARY_NEEDLE RGB(28,35,33)
#include "bb_kit_shelf.inc"
#define BB_SPARK_CORE RGB(255,222,134)
#define BB_SPARK_EMBER RGB(250,183,114)
#define BB_SPARK_RED RGB(243,158,141)
#define BB_SCOPE_INK RGB(241,188,139)
static bb_scope_column bb_scope_pixels[BB_SCOPE_COLUMNS];
static uint8_t bb_scope_ready;
static void bb_scope_snapshot(void) {
    bb_commands *q=&bb_sound.commands;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    bb_scope_ready=bb_sound.scope.valid;
    if(bb_scope_ready)memcpy(bb_scope_pixels,bb_sound.scope.published,sizeof bb_scope_pixels);
    if(q->leave)q->leave(q->lock_context,token);
}
static void bb_scope_backdrop(unsigned strip_y,unsigned height,int living) {
    if(!bb_scope_source())return;
    for(unsigned i=0;i<BB_SCOPE_COLUMNS;i++) {
        int low=bb_scope_ready?bb_scope_pixels[i].low:0;
        int high=bb_scope_ready?bb_scope_pixels[i].high:0;
        /* Fixed +/-1.0 range, no automatic gain. Clip to the mascot mask,
         * never the text; all controls are drawn above this afterwards. */
        int top=158-high*48/32767,bottom=158-low*48/32767;
        for(int y=top;y<=bottom;y++) {
            int py=living?80+(y-63)*4/5:y;
            if(py<(int)strip_y || py>=(int)(strip_y+height))continue;
            for(int dx=0;dx<3;dx++) {
                int x=24+(int)i*3+dx;
                unsigned bit=(unsigned)y*240u+(unsigned)x;
                if(bb_armadillo[bit>>3]&(1u<<(bit&7u))) {
                    if(living)x=24+x*4/5;
                    cv_pset(x,py-(int)strip_y,BB_SCOPE_INK);
                }
            }
        }
    }
}
static uint8_t bb_spark_level,bb_throb;
static unsigned bb_throb_step(unsigned current,unsigned target,unsigned elapsed) {
    if(elapsed>120u)elapsed=120u;
    if(current<target)return current+((target-current)*elapsed+119u)/120u;
    if(current>target)return current-((current-target)*elapsed+119u)/120u;
    return current;
}
/* Richer copper silhouette, warming red with the rendered sound. All128
 * quantized colours are checked against the foreground inks by the renderer
 * test. Keep the subdued BB_ART for disabled controls, not for the mascot. */
static uint16_t bb_mascot_ink(unsigned strength) {
    if(strength>127u)strength=127u;
    return RGB(219u+13u*strength/127u,154u-19u*strength/127u,99u+33u*strength/127u);
}
/* Fixed UI-only pool. Musical ticks release discrete charges; the frame
 * clock advances them between beats. No renderer work enters the audio ISR. */
#define BB_SPARK_COUNT 16u
typedef struct { uint32_t born; uint16_t life; uint8_t origin,seed; } bb_spark;
typedef struct {uint32_t born;uint8_t active;} bb_pb_wave;
typedef struct {bb_pb_wave wave[4];uint32_t tick,last_draw;unsigned slot,strength;uint8_t seen;} bb_pb_state;
static union {bb_spark sparks[BB_SPARK_COUNT];bb_pb_state playbook;} bb_visual_workspace;
static uint8_t bb_visual_workspace_owner;
#define bb_sparks (bb_visual_workspace.sparks)
#define bb_pb (bb_visual_workspace.playbook)
static uint32_t bb_spark_tick,bb_spark_release,bb_spark_rng=0x42454154u;
static uint8_t bb_spark_slot;
static void bb_sparks_update(void) {
    if(!bb_spark_level) {
        memset(bb_sparks,0,sizeof bb_sparks);bb_spark_tick=bb_ticks;
        bb_spark_release=fm1_ms;return;
    }
    uint32_t tick=bb_ticks;
    int running=bb_face.requested.running && tick;
    int emit=running?tick!=bb_spark_tick:(uint32_t)(fm1_ms-bb_spark_release)>=125u;
    bb_spark_tick=tick;
    if(!emit)return;
    bb_spark_release=fm1_ms;
    unsigned count=1u+bb_spark_level/32u;
    if(running && (tick-1u)%4u==0u)count++;
    for(unsigned i=0;i<count;i++) {
        bb_spark_rng=bb_spark_rng*1664525u+1013904223u;
        bb_spark *s=&bb_sparks[bb_spark_slot++%BB_SPARK_COUNT];
        s->born=fm1_ms;
        s->life=(uint16_t)(850u+((bb_spark_rng>>16)&511u));
        s->origin=(uint8_t)(30u+(bb_spark_rng%181u));
        s->seed=(uint8_t)(bb_spark_rng>>24);
    }
}
static void bb_sparks_draw(unsigned strip_y,unsigned height,int living) {
    unsigned motion=0;
#if BB_WITH_EFFECTS
    motion=bb_fx_parameter_get(&bb_face.requested,BB_FX_ECHO_MOTION);
#endif
    for(unsigned i=0;i<BB_SPARK_COUNT;i++) {
        const bb_spark *s=&bb_sparks[i];uint32_t age=fm1_ms-s->born;
        if(!s->life || age>=s->life)continue;
        unsigned travel=age*200u/s->life;
        int cy=214-(int)travel;
        int cx=(int)s->origin+(122-(int)s->origin)*(int)travel/200;
        /* A bounded triangle wander; Motion is a visual correspondence to
         * the requested echo setting, not a trace of its internal tape heads. */
        int phase=(int)((age/24u+s->seed)&31u);
        int wander=(phase<16?phase:31-phase)-8;
        cx+=wander*(int)(4u+motion/5u)/8;
        for(int tail=0;tail<4;tail++)for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++) {
            int x=cx+dx,y=cy+dy+tail*3;
            if(x<0 || x>=240 || y<0 || y>=240 || (living && y<63))continue;
            unsigned bit=(unsigned)y*240u+(unsigned)x;
            if(!(bb_armadillo[bit>>3]&(1u<<(bit&7u))))continue;
            uint16_t ink=tail?BB_SPARK_RED:bb_spark_level>48u?BB_SPARK_EMBER:BB_SPARK_RED;
            if(!tail && !dx && !dy && bb_spark_level>80u)ink=BB_SPARK_CORE;
            if(living){x=24+x*4/5;y=80+(y-63)*4/5;}
            if(y>=(int)strip_y && y<(int)(strip_y+height))cv_pset(x,y-(int)strip_y,ink);
        }
    }
}
static uint16_t bb_instrument_ink(void){return bb_face.requested.page==BB_TELEGRAM?BB_TELEGRAM_INK:BB_ORANGE;}
static void bb_instrument_tabs(void) {
    int top=bb_face.requested.page!=BB_DRUMS&&!bb_mix_page&&!bb_diag_page&&!bb_effect_page&&!bb_send_page?35:29;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    static const char *const titles[]={"DRUM A","DRUM B","DILLO"};
    unsigned selected=3;
    (void)st_from_editor(bb_face.requested.page,
        bb_face.requested.page==BB_DRUMS?bb_face.machine:0,&selected);
    for(unsigned row=0;row<3;row++) {
        int x=9+(int)row*75;
        uint16_t ink=row==selected?bb_instrument_ink():BB_DIM;
        cv_frame(x,top,72,23,ink);cv_text(x+4,top+7,&AF_S,titles[row],ink);
    }
#else
    static const bb_page pages[]={BB_DRUMS,
#if BB_WITH_DILLO
        BB_DILLO,
#endif
#if BB_WITH_TELEGRAM
        BB_TELEGRAM,
#endif
    };
    static const char *const titles[]={"DRUMS",BB_WITH_TELEGRAM?"BASS":"DILLO","LEAD"}; /* Dillo Duo: Dillo's two sides */
    unsigned count=sizeof pages/sizeof *pages;
    int width=222/(int)count;
    for(unsigned row=0;row<count;row++) {
        int x=9+(int)row*width;
        uint16_t ink=pages[row]==bb_face.requested.page?bb_instrument_ink():BB_DIM;
        cv_frame(x,top,width-3,23,ink);cv_text(x+4,top+7,&AF_S,titles[pages[row]],ink);
    }
#endif
}
#if BB_WITH_EVOLVE
/* The LCD is not touch-sensitive: K1 owns amount only on the explicit
 * Evolution page.  This main-loop-only transform makes the owned mascot's
 * actual head report that amount.  It never runs in the audio callback and
 * never mutates pattern state. */
static unsigned bb_evo_applied_amount(int *queued) {
    bb_commands *q=&bb_sound.commands;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    unsigned amount=bb_sound.evolve.ready?bb_sound.evolve.settings.amount:0u;
    if(queued)*queued=!bb_commands_idle(q);
    if(q->leave)q->leave(q->lock_context,token);
    return amount>100u?100u:amount;
}
static void bb_evolution_head_at(unsigned strip_y,unsigned height,uint16_t ink,int full) {
    if(!bb_evo_page)return;
    /* A queued knob request is not an applied amount. The numeric requested
     * setting remains visible with CONTROL QUEUED until the FIFO drains. */
    unsigned amount=bb_evo_applied_amount(NULL);
    if(!amount)return;
    static const uint16_t cosine[9]={256u,256u,256u,255u,255u,254u,253u,252u,250u};
    static const uint8_t sine[9]={0u,7u,13u,20u,27u,33u,40u,47u,53u};
    unsigned slot=(amount*8u+50u)/100u;
    int c=cosine[slot],s=sine[slot],cx=full?210:192,cy=full?180:174,radius=full?30:24;
    for(int ay=cy-radius;ay<=cy+radius;ay++)for(int x=cx-radius;x<=cx+radius;x++) {
        int dx=x-cx,dy=ay-cy;
        if(dx*dx+dy*dy>radius*radius || x<0 || x>=240 || ay<(int)strip_y || ay>=(int)(strip_y+height))continue;
        int sx=cx+(dx*c+dy*s)/256,sy=cy+(-dx*s+dy*c)/256;
        int ox=full?sx:(sx-24)*5/4,oy=full?sy:63+(sy-80)*5/4;
        cv_pset(x,ay-(int)strip_y,BB_BG);
        if(ox>=0 && ox<240 && oy>=0 && oy<240) {
            unsigned bit=(unsigned)oy*240u+(unsigned)ox;
            if(bb_armadillo[bit>>3]&(1u<<(bit&7u)))cv_pset(x,ay-(int)strip_y,ink);
        }
    }
}
static void bb_evolution_head(unsigned strip_y,unsigned height,uint16_t ink) {
    bb_evolution_head_at(strip_y,height,ink,0);
}
#endif
static void bb_backdrop(unsigned y,unsigned height,int pulse) {
    unsigned x,j;
    int living=bb_group_knobs_enabled((fm1_in.buttons&(1u<<panel.btn[B_REC]))!=0)
#if BB_WITH_EVOLVE
        || bb_evo_page
#endif
        ;
    uint16_t body=bb_mascot_ink(pulse==2?0:bb_throb);
    uint16_t tower=bb_mascot_ink(pulse==2?0:bb_throb+(pulse?48u:0u));
    /* Dense synth/editor labels keep priority over the full-size mascot.
     * The same measured throb still warms the backdrop, at lower luminance. */
    int synth_face=bb_face.requested.page!=BB_DRUMS&&!bb_mix_page&&!bb_diag_page&&
        !bb_effect_page&&!bb_send_page&&!bb_library_page&&!bb_sound_pick_page&&!bb_scene_page&&!bb_evo_page;
    if(synth_face && pulse!=2) {
        /* Keep the full mascot visible while the readouts remain dominant.
         * Its existing beat envelope warms this subdued copper palette. */
        body=RGB(105u+bb_throb*24u/127u,70u+bb_throb*8u/127u,48u+bb_throb*13u/127u);
        tower=RGB(100u+bb_throb*20u/127u,70u+bb_throb*6u/127u,48u+bb_throb*10u/127u);
    }
    if(!living && !synth_face && pulse!=2)body=tower=RGB(35u+bb_throb*18u/127u,
        30u+bb_throb*8u/127u,24u+bb_throb*4u/127u);
    /* Select the renderer's dark antialiasing curve and colour mode too:
     * inherited recovery GREY mode must not turn the orange identity gray.
     * This is main-loop drawing state only, never an audio operation. */
    if(ux.light || ux.mono || ux.bg!=BB_BG){ux.light=0;ux.mono=0;ux.bg=BB_BG;ux.gen++;}
    cv_begin(240,height,BB_BG);
    /* Utility pages need a quiet reading surface. Keep the owned artwork on
     * instrument faces and startup, without adding buffers or drawing passes.
     * Bank choices also need a quiet surface; their actions are unchanged. */
    if(pulse!=2 && ((bb_mix_page && !HM_VISIBLE) || bb_effect_page || bb_send_page))return;
    if(!living || pulse==2) {
        /* Eight pixels per mask byte; empty backdrop bytes cost no pixel tests.
         * 240px rows are byte-aligned. No extra bitmap or framebuffer. */
        for(j=0;j<height;j++)for(unsigned byte=0;byte<30u;byte++) {
            unsigned bits=bb_armadillo[(y+j)*30u+byte];
            if(!bits)continue;
            uint16_t ink=!living && y+j<63u?tower:body;
            if(synth_face && y+j<29u && pulse!=2)ink=BB_ART;
            for(unsigned k=0;k<8u;k++)if(bits&(1u<<k))
                cv_pset((int)(byte*8u+k),(int)j,ink);
        }
    } else {
        for(j=0;j<height;j++)for(x=0;x<240;x++) {
            if(x<24u || x>=216u || y+j<80u || y+j>=222u)continue;
            unsigned bit=(63u+(y+j-80u)*5u/4u)*240u+(x-24u)*5u/4u;
            if(bb_armadillo[bit>>3]&(1u<<(bit&7u)))
                cv_pset((int)x,(int)j,body);
        }
    }
#if BB_WITH_EVOLVE
    if(living && pulse!=2)bb_evolution_head(y,height,body);
#endif
    if(pulse!=2)bb_scope_backdrop(y,height,living);
    if(pulse!=2 && bb_spark_level)bb_sparks_draw(y,height,living);
    if(living && pulse!=2 && !y) {
        /* The upper strip is reserved for navigation and voice/action labels.
         * The animal and its sound feedback remain behind the lower controls. */
        cv_rect(0,0,240,120,BB_BG);
    }
}
static void bb_number(int x,int y,unsigned value,const aafont_t *font,uint16_t ink) {
    char s[12];fmt_int(s,(int32_t)value);cv_text(x,y,font,s,ink);
}
static void bb_swing_number(int x,int y,unsigned value,const aafont_t *font,uint16_t ink) {
    char s[8];unsigned x2=bb_swing_x2(value);fmt_int(s,(int32_t)(x2/2u));
    unsigned n=0;while(s[n])n++;
    if(x2&1u){s[n++]='.';s[n++]='5';s[n]=0;}
    cv_text(x,y,font,s,ink);
}
static void bb_level_number(int x,int y,unsigned value,const aafont_t *font,uint16_t ink) {
    char s[8];value=value>127u?127u:value;
    fmt_int(s,(int32_t)((value*100u+63u)/127u));
    int end=cv_text(x,y,font,s,ink);
    cv_text(end+1,y+(int)font->h-(int)AF_S.h,&AF_S,"%",ink);
}
/* UI-only chrome rotary: fixed integer work, no audio reads or trigonometry.
 * This shows the requested gain position, never pretends to be a VU meter. */
static void bb_rotary(int x,int y,unsigned value,int enabled) {
    static const int8_t tip[17][2]={
        {-11,11},{-14,8},{-16,3},{-16,-2},{-15,-6},{-12,-10},{-9,-13},{-5,-15},
        {0,-16},{5,-15},{9,-13},{12,-10},{15,-6},{16,-2},{16,3},{14,8},{11,11}};
    for(int j=-20;j<=20;j++)for(int i=-20;i<=20;i++) {
        int r=i*i+j*j;
        if(r<=400) {
            unsigned shade=r>361?80u:r>256?(unsigned)(230-j*3):(unsigned)(200-j*2+(j&1)*4);
            if(shade>255u)shade=255u;
            cv_pset(x+i,y+j,enabled?RGB(shade,shade,shade):BB_ART);
        }
    }
    if(enabled) {
        unsigned scaled=value*16u,slot=scaled/127u,fraction=scaled%127u;
        int dx=tip[slot][0],dy=tip[slot][1];
        if(slot<16u) {
            dx+=(tip[slot+1u][0]-dx)*(int)fraction/127;
            dy+=(tip[slot+1u][1]-dy)*(int)fraction/127;
        }
        for(int i=0;i<=16;i++)cv_rect(x+dx*i/16,y+dy*i/16,2,2,BB_ROTARY_NEEDLE);
    }
}
static void bb_pitch_name(char name[6],unsigned pitch) {
    static const char *const names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const char *p=names[pitch%12u];unsigned n=0;
    while(*p)name[n++]=*p++;
    if(pitch<12u){name[n++]='-';name[n++]='1';}
    else name[n++]=(char)('0'+pitch/12u-1u);
    name[n]=0;
}
#include "bb_nav_draw.inc"
/* Navigation v2.1: the strip shows bb_nav's K1-K4 labels and values. */
static void bb_synth_knob_strip(int label_y,int value_y,const aafont_t *value_font) {
    bb_nav_knob_strip(label_y,value_y,value_font);
}
#if BB_WITH_RICH_DRUM_BROWSER
/* Only the source-pattern face uses this strip. Pads, tone editing and mixer
 * pages have different assignments and must retain their own labels. */
static int bb_rich_step_face(void) {
    return bb_face.requested.page==BB_DRUMS&&bb_rich_active()&&!bb_mix_page&&
        !bb_diag_page&&!bb_effect_page&&!bb_send_page&&!bb_pad_page&&
        !bb_kit_page&&!bb_drum_sound_page;
}
static void bb_rich_knob_strip(void) {
    bb_nav_knob_strip(0,14,&AF_M);
}
#endif
static void bb_synth_draw(void) {
    unsigned synth=bb_selected_synth(),k,head=bb_synth_playhead[synth];
    const bb_sequence *seq=&bb_face.requested.synths[synth];
    if(bb_keys_page[synth]) {
        unsigned white=0;char pitch[6];
        for(k=0;k<27;k++)if(bb_panel_white_step(k)>=0) {
            int x=8+(int)white++*14;
            if(bb_keys_sent[synth]&(1u<<k))cv_rect(x,11,12,32,bb_instrument_ink());
            else cv_frame(x,11,12,32,BB_TEXT);
        }
        white=0;
        for(k=0;k<27;k++) {
            if(bb_panel_white_step(k)>=0)white++;
            else cv_rect(4+(int)white*14,11,7,18,(bb_keys_sent[synth]&(1u<<k))?BB_GREEN:BB_TEXT);
        }
        char shifted[5];unsigned amount=(unsigned)(bb_pattern_shift[synth]<0?-bb_pattern_shift[synth]:bb_pattern_shift[synth]);
        unsigned n=0;shifted[n++]=bb_pattern_shift[synth]<0?'-':'+';
        if(amount>=10u)shifted[n++]=(char)('0'+amount/10u);
        shifted[n++]=(char)('0'+amount%10u);shifted[n]=0;
        bb_pitch_name(pitch,bb_keys_base[synth]);cv_text(9,47,&AF_M,"KEY",BB_TEXT);cv_text(42,47,&AF_M,pitch,BB_TEXT);
        cv_text(80,47,&AF_M,"PAT TMP",BB_TEXT);cv_text(148,47,&AF_M,shifted,BB_TEXT);
    } else for(k=0;k<16;k++) {
        unsigned step=bb_face.bank*16u+k;int x=8+(int)k*14;
        const bb_note *note=&seq->notes[step];
        uint16_t ink=step<seq->length?BB_DIM:BB_ART;
        if(step<seq->length && note->gate) {
            ink=note->accent?BB_TEXT:bb_instrument_ink();cv_rect(x,11,11,25,ink);
            if(note->slide)cv_rect(x,38,11,3,BB_GREEN);
        } else cv_frame(x,11,11,25,ink);
        if(bb_face.requested.running && step==head)cv_rect(x,3,11,4,BB_GREEN);
        if(step==bb_selected_note[synth])cv_rect(x,45,11,3,BB_TEXT);
        if(!(k&3u))bb_number(x,50,step+1,&AF_S,BB_DIM);
    }
    cv_text(9,67,&AF_S,"PRESETS: SOUND / ALGO: PATTERN",BB_DIM);
    cv_text(9,83,&AF_S,"GLO: GLOBAL / FX: ECHO + SPRING",BB_DIM);
    const char *scale_hint=bb_nav_scale_hint();
    const char *hint=bb_tone_page==1?"SOUND / K4: WAVE / HOME: STUDIO":
        bb_keys_page[synth]?(scale_hint?scale_hint:bb_pattern_shift[synth]?"TRANSPOSED / GLOBAL K4":"OCT: KEYS ONLY / SEQ: STEPS"):
        bb_face.requested.edit==BB_ACCENT?"ACCENT / EDIT: NEXT TYPE":bb_face.requested.edit==BB_SLIDE?"SLIDE / EDIT: NEXT TYPE":"GATE / EDIT: NEXT / OCT: BANK";
    cv_text(9,105,&AF_S,bb_hint_until?bb_hint:bb_scope_visible()?"11.6 MS / GLO WHITE 2: OFF":hint,BB_DIM);
}
#if BB_WITH_EVOLVE
static const char *bb_evo_status(int queued) {
    return bb_face.rejected?"BUSY / RETRY":!bb_evo_view.ready?"WAIT":bb_evo_view.result==BB_EV_RESULT_REJECTED?"REJECTED":
        bb_evo_view.pending?"RETURN WAIT":bb_evo_view.conflict?"EDIT / FROZEN":
        queued?"CONTROL QUEUED":!bb_evo_view.enabled?"OFF":bb_evo_view.frozen?"FROZEN":"GROWING";
}
static void bb_evo_draw(void) {
    const uint16_t text=BB_TEXT; /* Keep orange artwork/outlines; labels follow the surface palette. */
    static const char *const roots[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char *const parts[]={"1 KIT A","2 KIT B","3 KIT C","4 KIT D","5 DILLO","6 TELEGR"};
    static const char *const attrs[]={"7 RHY","8 NOTE","9 ACC","10 SLD"};
    int queued=0;(void)bb_evo_applied_amount(&queued);
    const char *status=bb_evo_status(queued);
    bb_backdrop(0,120,0);cv_text(9,6,&AF_M,"EVOLVE",text);
    cv_text(126,11,&AF_S,status,text);
    for(unsigned k=0;k<4;k++)cv_text(9+(int)k*56,43,&AF_S,bb_nav_knob_label(k),text);
    bb_number(9,64,bb_evo_view.settings.amount,&AF_M,text);
    bb_number(65,64,bb_evo_view.settings.every_phrases,&AF_M,text);
    cv_text(121,64,&AF_M,roots[bb_evo_view.settings.root%12u],text);
    cv_text(177,68,&AF_S,bb_evo_view.settings.scale==0x5ad?"MINOR":bb_evo_view.settings.scale==0xab5?"MAJOR":"CUSTOM",text);
    cv_text(9,105,&AF_S,bb_hint_until?bb_hint:"REC+WHITE: LOCK / HOME: STUDIO",text);cv_blit(0,0);
    bb_backdrop(120,120,0);
    if(bb_evo_rec) {
        unsigned part=bb_face.requested.page==BB_DRUMS?bb_face.machine:BB_MACHINES+bb_selected_synth();
        cv_text(9,4,&AF_S,"LOCKS",text);cv_text(63,4,&AF_S,parts[part]+2,text);
        for(unsigned k=0;k<16;k++) {
            unsigned step=bb_face.bank*16u+k;
            int x=9+(int)(k%8)*28,y=24+(int)(k/8)*30;
            uint16_t ink=bb_evo_view.settings.locked[part]&(UINT64_C(1)<<step)?BB_ORANGE:BB_DIM;
            cv_frame(x,y,25,25,ink);bb_number(x+4,y+8,step+1,&AF_S,text);
            if(bb_evo_view.settings.locked[part]&(UINT64_C(1)<<step))cv_rect(x+2,y+2,5,4,ink);
        }
        cv_text(9,88,&AF_S,"LIT MARK: PROTECTED STEP",text);
        cv_text(9,105,&AF_S,"RELEASE REC: PARTS / CHANGES",text);cv_blit(0,120);return;
    }
    /* Keep the head clear on the right: these selectors become a compact
     * two-column control bank integrated over the body, not text across the
     * character's face.  The key LEDs retain the same six-part ownership. */
    for(unsigned k=0;k<6;k++) {
        int x=9+(int)(k%2)*76,y=4+(int)(k/2)*18;
        uint16_t ink=bb_evo_view.settings.parts&(1u<<k)?BB_ORANGE:BB_DIM;
        cv_frame(x,y,70,16,ink);cv_text(x+3,y+2,&AF_S,parts[k],text);
        if(bb_evo_view.settings.parts&(1u<<k))cv_rect(x+2,y+2,4,3,ink);
    }
    for(unsigned k=0;k<4;k++) {
        int x=9+(int)k*40;uint16_t ink=bb_evo_view.settings.attributes&(1u<<k)?BB_ORANGE:BB_DIM;
        cv_frame(x,61,37,18,ink);cv_text(x+2,64,&AF_S,attrs[k],text);
        if(bb_evo_view.settings.attributes&(1u<<k))cv_rect(x+2,63,4,3,ink);
    }
    cv_text(9,87,&AF_S,"REC+LFO: ON/OFF / EDIT: CAPTURE",text);
    cv_text(9,105,&AF_S,"BLACK 1: FREEZE / 2: RETURN",text);cv_blit(0,120);
}
#endif
/* Navigation v2.1: the guide describes bb_nav's map for the page under it. */
static void bb_help_draw(void) {
    const char *rows[9];
    const char *title=bb_nav_help_rows(rows);
    uint16_t ink=bb_face.requested.page==BB_DRUMS?BB_ORANGE:bb_face.requested.page==BB_DILLO?BB_GREEN:BB_TELEGRAM_INK;
    bb_backdrop(0,120,0);cv_text(9,6,&AF_M,title,ink);
    for(unsigned i=0;i<4;i++)cv_text(9,35+(int)i*23,&AF_S,rows[i],BB_TEXT);
    cv_blit(0,0);bb_backdrop(120,120,0);
    for(unsigned i=4;i<9;i++)cv_text(9,8+(int)(i-4)*23,&AF_S,rows[i],BB_TEXT);
    cv_blit(0,120);
}
#if BB_WITH_BROWSER
/* Full source strings stay in the catalogue. This display explicitly uses
 * ellipses when a title exceeds one line; numbering keeps similar names distinct. */
static void bb_library_text(bb_catalogue_id id,const aafont_t *font,int width,char *out) {
    const uint8_t *bytes;size_t count;
    if(!bb_catalogue_text(&bb_factory_catalogue,id,&bytes,&count)){out[0]=0;return;}
    size_t n=count<120?count:120;
    for(size_t i=0;i<n;i++)out[i]=bytes[i]>=32 && bytes[i]<=126?(char)bytes[i]:'?';
    out[n]=0;
    if(count>n || text_w(font,out)>width) {
        while(n && (n>117 || text_w(font,out)>width-14)){out[--n]=0;}
        out[n++]='.';out[n++]='.';out[n]=0;
    }
}
static void bb_library_label(int y,const char *prefix,const bb_player_selection *selection) {
    char title[128];int x=cv_text(9,y,&AF_S,prefix,BB_DIM);
    if(!selection->ticket){cv_text(x+3,y,&AF_S,"CURRENT PATTERN",BB_TEXT);return;}
    bb_catalogue_id id=bb_catalogue_find(&bb_factory_catalogue,selection->choice.bank,
                                       selection->choice.group?"song":"name");
    bb_number(x+3,y,selection->choice.part_index+1u,&AF_S,BB_TEXT);
    bb_library_text(id,&AF_S,231-x-24,title);cv_text(x+24,y,&AF_S,title,BB_TEXT);
}
static const char *bb_library_kit_name(unsigned id) {
    if(id==BB_SAMPLE_808_ID)return "808";
    return id<bb_sample_ui_count&&bb_sample_ui_names?bb_sample_ui_names[id]:"UNAVAILABLE";
}
static void bb_library_draw(int pulse) {
    unsigned synth=bb_selected_synth();int drums=bb_face.requested.page==BB_DRUMS;
    bb_player *p=bb_library_current();const bb_machine *machine=&bb_face.requested.drums[bb_face.machine];
    char title[128];bb_factory_choice choice;
    /* Only the selected kit's playable cards look selectable. Catalogue
     * decoding happens on a changed page/kit, not each animated frame. The
     * audio-owner commit still revalidates every attempted pair. */
    static struct {
        const bb_catalogue *catalogue;
        uint16_t bank_mask,bank_index,bank_page,part_page;
        uint8_t part_mask,machine,group,kit,valid,bank_scan,part_scan;
    } visible;
    unsigned preview_kit=0;
    int filtered=drums&&bb_library_preview_kit(p,&preview_kit);
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    /* Decode only when the highlighted pair changes, never on audio thread.
     * A preview notice must not masquerade as the currently playing pair. */
    static struct {
        const bb_catalogue *catalogue;
        uint16_t bank,part;
        uint8_t valid,group,kit,machine,count,first,replacement,known;
    } substitutes;
    if(filtered && (!substitutes.valid || substitutes.catalogue!=p->view.catalogue ||
       substitutes.bank!=p->view.bank_index || substitutes.part!=p->view.part_index ||
       substitutes.group!=p->view.group || substitutes.kit!=preview_kit || substitutes.machine!=p->target)) {
        unsigned count=0,first=255,replacement=255;
        substitutes.known=bb_player_preview_substitutions(p,&bb_sound,preview_kit,&count,&first,&replacement);
        substitutes.catalogue=p->view.catalogue;substitutes.bank=p->view.bank_index;
        substitutes.part=p->view.part_index;substitutes.group=p->view.group;
        substitutes.kit=(uint8_t)preview_kit;substitutes.machine=p->target;
        substitutes.count=(uint8_t)count;substitutes.first=(uint8_t)first;
        substitutes.replacement=(uint8_t)replacement;substitutes.valid=1;
    }
    if(filtered && (!visible.valid || visible.catalogue!=p->view.catalogue ||
        visible.machine!=p->target || visible.kit!=preview_kit ||
        visible.group!=p->view.group || visible.bank_index!=p->view.bank_index ||
        visible.bank_page!=p->view.bank_page || visible.part_page!=p->view.part_page)) {
        visible.bank_mask=0;visible.part_mask=0;
        visible.bank_scan=0;visible.part_scan=0;
        visible.catalogue=p->view.catalogue;visible.machine=(uint8_t)p->target;
        visible.kit=(uint8_t)preview_kit;visible.group=p->view.group;
        visible.bank_index=p->view.bank_index;visible.bank_page=p->view.bank_page;
        visible.part_page=p->view.part_page;visible.valid=1;
    }
    if(filtered) {
        for(unsigned n=0;n<2 && visible.bank_scan<BB_BROWSER_BANK_KEYS;n++){
            unsigned k=visible.bank_scan++;
            if(bb_browser_bank_cell(&p->view,k)==BB_CATALOGUE_NONE)continue;
            bb_player candidate=*p;
            if(bb_player_preview_bank_key_kit(&candidate,&bb_sound,preview_kit,k))
                visible.bank_mask|=(uint16_t)(1u<<k);
        }
        for(unsigned n=0;n<2 && visible.part_scan<BB_BROWSER_PART_KEYS;n++){
            unsigned k=visible.part_scan++;
            if(bb_browser_part_cell(&p->view,k)==BB_CATALOGUE_NONE)continue;
            bb_player candidate=*p;
            if(bb_player_preview_part_key_kit(&candidate,&bb_sound,preview_kit,k))
                visible.part_mask|=(uint8_t)(1u<<k);
        }
        if(visible.bank_scan<BB_BROWSER_BANK_KEYS || visible.part_scan<BB_BROWSER_PART_KEYS)bb_dirty=1;
    }
#else
    filtered=0;
#endif
    bb_backdrop(0,120,pulse);
    /* Navigation v2.1: K1-K4 labels on top for drums too. */
    bb_nav_knob_strip(0,14,&AF_S);
    if(drums && bb_kit_intent[bb_face.machine].valid) {
        char next_kit[]="A / NEXT KIT:";next_kit[0]=(char)('A'+bb_face.machine);
        int x=cv_text(9,28,&AF_S,next_kit,BB_DIM);
        cv_text(x+3,28,&AF_S,bb_library_kit_name(bb_kit_intent[bb_face.machine].id),BB_TEXT);
    } else cv_text(9,28,&AF_S,drums?(p->view.group?(bb_face.machine?"DRUM B / SONG":"DRUM A / SONG"):
        (bb_face.machine?"DRUM B / ERA":"DRUM A / ERA")):
#if BB_WITH_TELEGRAM
        synth?(p->view.group==BB_BROWSER_ACID?"DILLO LEAD / ACID":p->view.group?"DILLO LEAD / SONG":"DILLO LEAD / ERA"):
#endif
        (p->view.group==BB_BROWSER_ACID?(BB_WITH_TELEGRAM?"DILLO BASS / ACID":"DILLO / ACID"):
         p->view.group?(BB_WITH_TELEGRAM?"DILLO BASS / SONG":"DILLO / SONG"):(BB_WITH_TELEGRAM?"DILLO BASS / ERA":"DILLO / ERA")),BB_DIM);
    int muted=drums?machine->muted:bb_face.requested.synths[synth].muted;
    int enabled=!drums || machine->enabled;
    unsigned level=drums?machine->level:bb_face.requested.band_level[synth+1u];
    cv_text(158,28,&AF_S,!enabled?"OFF":muted?"MUTED":"ON",BB_TEXT);
    bb_number(199,28,(level*100u+63u)/127u,&AF_S,BB_TEXT);
    cv_text(222,28,&AF_S,"%",BB_TEXT);
    cv_rect(7,46,226,40,BB_GREEN);
    if(bb_browser_current(&p->view,&choice)) {
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,choice.bank,choice.group?"song":"name"),&AF_M,222,title);
        cv_text_on(9,49,&AF_M,title,BB_BG,BB_GREEN);
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,choice.part,"name"),&AF_S,222,title);
        cv_text_on(9,71,&AF_S,title,BB_BG,BB_GREEN);
    }
    bb_library_label(89,bb_face.requested.running && enabled && !muted?"PLAYING:":"LOADED:",&p->playing);
    if(bb_player_pending(p))bb_library_label(105,bb_player_pending(p)==2?"QUEUED:":"REQUEST:",&p->request);
    else if(p->error)cv_text(9,105,&AF_S,bb_library_error_text(p->error),BB_DIM);
    else if(!enabled)cv_text(9,105,&AF_S,"LAYER OFF / PATTERN IS SILENT",BB_DIM);
    else if(drums && p->playing.ticket && p->playing.has_kit) {
        /* The highlighted choice is not the audio owner's applied kit.
         * Keep the applied identity visible while browsing another pair. */
        int x=cv_text(9,105,&AF_S,"LOADED KIT:",BB_DIM);
        cv_text(x+3,105,&AF_S,bb_library_kit_name(p->playing.kit),BB_TEXT);
    } else cv_text(9,105,&AF_S,bb_hint_until?bb_hint:"ALGO: BROWSE / EDIT: CHOOSE",BB_DIM);
    cv_blit(0,0);bb_backdrop(120,120,pulse);
    /* Eight readable names, all sixteen fixed physical assignments.
     * Selecting key9..16 shows that half; no extra page gesture or remap.
     * The large card above retains the full-width name. A cursor is not
     * proof of a load: only an applied ticket gets the filled row. */
    unsigned first=(p->view.bank_index%16u)>=8u?8u:0u;
    for(unsigned row=0;row<8;row++) {
        unsigned k=first+row;
        int x=8+(int)(row%2u)*115,y=2+(int)(row/2u)*21;
        bb_catalogue_id bank=bb_browser_bank_cell(&p->view,k);
        unsigned selected=p->view.bank_index==p->view.bank_page*16u+k;
        if(bank==BB_CATALOGUE_NONE || (filtered && !(visible.bank_mask&(1u<<k))))continue;
        unsigned applied=p->playing.ticket && p->playing.choice.bank==bank;
        uint16_t ink=applied?BB_TEXT:BB_BG;
        uint16_t bg=applied?BB_ART:BB_GREEN;
        cv_rect(x,y,109,20,bg);
        cv_frame(x,y,109,20,selected?BB_SPARK_EMBER:BB_DIM);
        char number[12];fmt_int(number,(int32_t)k+1);
        cv_text_on(x+3,y+2,&AF_S,number,ink,bg);
        bb_library_text(bb_catalogue_find(&bb_factory_catalogue,bank,p->view.group?"song":"name"),&AF_S,84,title);
        cv_text_on(x+22,y+2,&AF_S,title,ink,bg);
        if(bb_player_pending(p) && p->request.choice.bank==bank)
            cv_rect(x+3,y+18,103,2,BB_SPARK_EMBER);
    }
    /* Navigation v2.1: black keys 1-11 are sections, paged by REC+OCT;
     * the boxes show the section numbers of the page in use. */
    {
        unsigned count=(unsigned)bb_browser_part_count(&p->view),page=bb_nav_section_page(&bb_nav);
        for(unsigned k=0;k<BB_NAV_SECTION_KEYS;k++) {
            unsigned index=page*BB_NAV_SECTION_KEYS+k;
            if(index>=count)break;
            int x=9+(int)k*20;char label[4];fmt_int(label,(int32_t)index+1);
            unsigned selected=p->view.part_index==index;
            unsigned playing=p->playing.ticket&&p->playing.choice.group==p->view.group&&
                p->playing.choice.bank_index==p->view.bank_index&&p->playing.choice.part_index==index;
            if(playing)cv_rect(x,86,18,18,BB_ART);
            cv_frame(x,86,18,18,selected?bb_instrument_ink():BB_DIM);
            cv_text(x+(18-text_w(&AF_S,label))/2,87,&AF_S,label,BB_TEXT);
        }
    }
    const char *section_hint=bb_nav_section_hint();
    const char *footer=filtered && visible.bank_scan<BB_BROWSER_BANK_KEYS?
        "CHECKING PLAYABLE PATTERNS":section_hint?section_hint:"ALGO: BROWSE / EDIT: CHOOSE";
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
    if(filtered && substitutes.known && substitutes.count && visible.bank_scan==BB_BROWSER_BANK_KEYS) {
        /* Selected-pair footer; applied kit and playing pattern remain above. */
        int x=cv_text(9,105,&AF_S,"SUB ",BB_TEXT);
        x=cv_text(x,105,&AF_S,substitutes.first==BB_PHRASE_SHAKER?"SH":bb_factory_drum_voice_name(substitutes.first),BB_TEXT);
        x=cv_text(x,105,&AF_S,">",BB_TEXT);
        x=cv_text(x,105,&AF_S,substitutes.replacement==BB_PHRASE_SHAKER?"SH":bb_factory_drum_voice_name(substitutes.replacement),BB_TEXT);
        if(substitutes.count>1){char more[12];fmt_int(more,substitutes.count-1);x=cv_text(x,105,&AF_S,"+",BB_TEXT);x=cv_text(x,105,&AF_S,more,BB_TEXT);}
        cv_text(x,105,&AF_S," / EDIT",BB_TEXT);footer=NULL;
    }
#endif
    if(footer)cv_text(9,105,&AF_S,footer,BB_TEXT);
    cv_blit(0,120);
}
#include "bb_home_draw.inc"
#include "home_master/screen.inc"
#include "home_master/integration.inc"
#endif
#include "bb_sound_picker_draw.inc"
#include "bb_playbook_face.inc"
#include "bb_sample_sends_draw.inc"
#include "bb_ui_v3_firmware.inc"
#include "bb_midi_ui_draw.inc"
static void bb_target_draw(void) {
    if(bb_asset_hold_request)return;
    bb_pb_own_workspace();
    static uint8_t last_head=255,last_accent=255,last_pulse;
    static uint32_t last_beat=UINT32_MAX;
    static uint32_t next_draw,pulse_until,next_meter,last_visual;
    unsigned k,head=bb_face.requested.page==BB_DRUMS || bb_mix_page?bb_playhead[bb_face.machine]:bb_synth_playhead[bb_selected_synth()];uint8_t pulse;
    bb_machine *m=&bb_face.requested.drums[bb_face.machine];
    if(ui.uboot){ui_draw_update();bb_dirty=1;return;}
    if(bb_midi_ui_draw())return;
    if(bb_v3_draw())return;                       /* every player-facing page (bb_ui_v3_route.h) */
    if(bb_nav.page==BB_NAV_GLOBAL&&!bb_help_visible) {
        /* Navigation v2.1: GLOBAL face; meters refresh twice a second. */
        if(bb_hint_until && (int32_t)(fm1_ms-bb_hint_until)>=0){bb_hint_until=0;bb_hint="";bb_dirty=1;}
        if(bb_nav_meters && (int32_t)(fm1_ms-bb_global_dirty_at)>=0){bb_global_dirty_at=fm1_ms+500u;bb_dirty=1;}
        if(bb_dirty){bb_dirty=0;bb_nav_global_draw();}
        return;
    }
    if(bb_sound_pick_visible()&&!bb_help_visible){if(bb_dirty){bb_dirty=0;bb_sound_pick_draw();}return;}
    if(bb_help_visible) {
        if(bb_dirty){bb_dirty=0;bb_help_draw();}
        return;
    }
#if BB_WITH_SESSION
    if(bb_scene_page) {
        if(!bb_dirty)return;
        bb_dirty=0;
        bb_backdrop(0,120,0);cv_text(9,6,&AF_M,"BIG BEAT / SCENES",BB_TEXT);
        cv_text(9,42,&AF_L,"SCENE",BB_TEXT);bb_number(176,42,bb_scene_slot+1,&AF_L,BB_ORANGE);
        cv_text(9,83,&AF_S,"PATTERNS / SOUNDS / LEVELS",BB_DIM);
        cv_text(9,105,&AF_S,bb_hint,BB_TEXT);cv_blit(0,0);
        bb_backdrop(120,120,0);
        for(k=0;k<4;k++) {
            char label[8]={'S','C','E','N','E',' ',(char)('1'+k),0};
            uint16_t ink=k==bb_scene_slot?BB_ORANGE:BB_DIM;
            cv_frame(9,3+(int)k*22,222,20,ink);cv_text(17,8+(int)k*22,&AF_S,label,BB_TEXT);
        }
        cv_text(9,90,&AF_S,"K1 OR WHITE 1-4: SLOT",BB_DIM);
        cv_text(9,105,&AF_S,bb_face.requested.running?"STOP TO STORE OR LOAD":"EDIT: STORE / REC+WHITE: LOAD",BB_TEXT);cv_blit(0,120);return;
    }
#endif
#if BB_WITH_EVOLVE
    if(bb_evo_page) {
        bb_evo_sync();bb_scope_sync();
        if(bb_pb_evo_active()){bb_pb_evo_frame();return;}
        if(!bb_dirty || (int32_t)(fm1_ms-next_draw)<0)return;
        next_draw=fm1_ms+33u;bb_dirty=0;bb_evo_draw();return;
    }
#endif
    bb_scope_sync();
    if(bb_pb_active()){bb_pb_frame();return;}
    if(bb_scope_source())bb_dirty=1;
    /* Measured output drives strength, including stopped live pads and FX
     * tails. Clock drives travel only; silence is not a pretend waveform. */
    {
        unsigned level=bb_face.animate && !bb_diag_page?bb_visual_peak/128u:0u;
        if(level>127u)level=127u;
        if(level<2u)level=0u;
        if(level!=bb_spark_level || level || bb_throb)bb_dirty=1;
        bb_spark_level=level;
    }
    if(bb_diag_page && (int32_t)(fm1_ms-next_meter)>=0){next_meter=fm1_ms+500u;bb_dirty=1;}
    if(head!=last_head)bb_dirty=1;
    /* Sixteenth ticks are published by the audio adapter; pulse on each new
     * quarter, independent of per-drum lengths and playheads. */
    if(bb_face.requested.running && bb_ticks) {
        uint32_t beat=(bb_ticks-1u)/4u;
        if(beat!=last_beat){last_beat=beat;if((bb_ticks-1u)%4u==0u)pulse_until=fm1_ms+90u;bb_dirty=1;}
    } else last_beat=UINT32_MAX;
    pulse=(uint8_t)(bb_face.animate && bb_face.requested.running && (int32_t)(pulse_until-fm1_ms)>0);
    if(pulse!=last_pulse || last_accent!=bb_face.accent)bb_dirty=1;
    if(bb_hint_until && (int32_t)(fm1_ms-bb_hint_until)>=0){bb_hint_until=0;bb_hint="";bb_dirty=1;}
    if(!bb_dirty || (int32_t)(fm1_ms-next_draw)<0)return;
    /* Time-based easing: a quiet/off screen settles, rather than jumping
     * between saturated colours every audio block. No full-field strobe. */
    {
        unsigned dt=fm1_ms-last_visual;last_visual=fm1_ms;
        if(!bb_face.animate || bb_diag_page)bb_throb=0;
        else bb_throb=bb_throb_step(bb_throb,bb_spark_level,dt);
    }
    next_draw=fm1_ms+33u;bb_dirty=0;last_head=(uint8_t)head;last_accent=bb_face.accent;last_pulse=pulse;
#if BB_SAMPLE_HOST_INTEGRATION && BB_WITH_EFFECTS
    bb_sample_status sample_send_view;
    int sample_send_route=bb_sample_sends_view(&sample_send_view);
#endif
    if(bb_scope_source())bb_scope_snapshot();
    bb_sparks_update();
#if BB_WITH_BROWSER
    if(HM_VISIBLE && !bb_scene_page && !bb_evo_page){hm_native_draw(pulse);return;}
    if(bb_mix_page==BB_MIX_BAND_PAGE && !bb_scene_page && !bb_evo_page){bb_home_draw(pulse);return;}
    if(bb_library_page){bb_library_draw(pulse);return;}
#endif
    /* Two reusable strips, not another 115 KB framebuffer. The tower continues
     * through the title bar; no duplicate corner mascot. */
    bb_backdrop(0,120,pulse);
    int synth_strip=!bb_mix_page&&!bb_diag_page&&!bb_effect_page&&!bb_send_page&&bb_face.requested.page!=BB_DRUMS;
    if(synth_strip)bb_synth_knob_strip(0,14,&AF_M);
#if BB_WITH_RICH_DRUM_BROWSER
    else if(bb_rich_step_face())bb_rich_knob_strip();
#endif
    else {
        cv_text(9,6,&AF_M,"BIG BEAT",BB_TEXT);
        cv_text(142,7,&AF_S,bb_face.requested.running?"PLAY":"STOP",BB_GREEN);
        bb_number(191,5,bb_face.requested.bpm,&AF_M,BB_TEXT);
    }
    if(bb_mix_page!=BB_MIX_KITS_PAGE && !bb_diag_page) {
        /* The strip follows the same physical rows as SELECT and HOME. */
        bb_instrument_tabs();
    } else for(k=0;k<BB_MACHINES;k++) {
        if(!bb_mix_control(BB_MIX_KITS_PAGE,k))continue;
        int x=9+(int)k*56;uint16_t ink=k==bb_face.machine && (bb_face.requested.page==BB_DRUMS || bb_mix_page)?BB_ORANGE:BB_DIM;
        cv_frame(x,29,52,23,ink);char label[2]={(char)('A'+k),0};cv_text(x+5,33,&AF_M,label,ink);
        cv_text(x+23,36,&AF_S,!bb_face.requested.drums[k].enabled?"OFF":bb_face.requested.drums[k].muted?"M":"ON",ink);
    }
    if(bb_effect_page) {
        cv_text(9,62,&AF_M,"SHARED EFFECTS",BB_TEXT);
        cv_text(9,87,&AF_S,"ECHO TAPE SPRING MOTION SENDS",BB_DIM);
        cv_text(9,105,&AF_S,bb_hint_until?bb_hint:"EDIT: NEXT BANK / HOME: STUDIO",BB_DIM);
    } else if(bb_mix_page) {
        cv_text(9,62,&AF_M,bb_mix_page!=BB_MIX_KITS_PAGE?"BAND MIXER":"DRUM MIXER",BB_TEXT);
        const char *mixer_parts;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        mixer_parts=bb_mix_page!=BB_MIX_KITS_PAGE?"DRUM A / B / DILLO":"TWO INDEPENDENT DRUMS";
#elif BB_WITH_TELEGRAM
        mixer_parts=bb_mix_page!=BB_MIX_KITS_PAGE?"DRUMS / BASS / LEAD":"DRUM LAYER LEVELS";
#elif BB_WITH_DILLO
        mixer_parts=bb_mix_page!=BB_MIX_KITS_PAGE?"DRUMS / DILLO":"DRUM LAYER LEVELS";
#else
        mixer_parts="DRUM LAYER LEVELS";
#endif
        cv_text(9,87,&AF_S,bb_hint_until?bb_hint:mixer_parts,BB_TEXT);
        cv_text(9,105,&AF_S,bb_mix_page==BB_MIX_DIRECT_PAGE?"GLO: BACK / FX: EFFECTS":"HOME: PLAY / REC+HOME: KITS",BB_DIM);
    } else if(bb_face.requested.page!=BB_DRUMS) {
        unsigned synth=bb_selected_synth();char pitch[6];
        const bb_sequence *seq=&bb_face.requested.synths[synth];
        bb_pitch_name(pitch,bb_keys_page[synth]?bb_keys_base[synth]:seq->notes[bb_selected_note[synth]].note);
        cv_text(9,64,&AF_L,synth?"DILLO LEAD":BB_WITH_TELEGRAM?"DILLO BASS":"DILLO",bb_instrument_ink());
        bb_number(142,73,bb_face.requested.bpm,&AF_S,BB_TEXT);
        cv_text(165,73,&AF_S,"BPM",BB_DIM);
        cv_text(196,73,&AF_S,seq->muted?"MUTE":bb_face.requested.running?"PLAY":"READY",seq->muted?BB_DIM:BB_GREEN);
        if(bb_patch_edited[synth])cv_text(108,73,&AF_S,"EDIT",BB_DIM);
        cv_text(9,97,&AF_S,bb_patch_from_scene[synth]==2?"ERA SOUND":bb_patch_from_scene[synth]?"SCENE SOUND":synth?bb_telegram_bank[bb_patch_index[synth]].name:bb_dillo_bank[bb_patch_index[synth]].name,BB_TEXT);
        cv_text(142,97,&AF_S,bb_keys_page[synth]?"KEYS":"STEP",BB_DIM);
        if(!bb_keys_page[synth])bb_number(173,97,bb_selected_note[synth]+1,&AF_S,BB_TEXT);
        cv_text(201,97,&AF_S,pitch,BB_TEXT);
    } else {
        cv_text(9,62,&AF_M,bb_ui_kit_name(bb_face.machine),BB_TEXT);
        char layer[6]={'K','I','T',' ',(char)('A'+bb_face.machine),0};
        cv_text(190,65,&AF_S,layer,BB_TEXT);
        /* Enter a named kit first; the selected sound is subordinate, not an
         * apparent single-instrument dead end. Both retain readable type. */
        if(bb_kit_page) {
            unsigned active=0;for(k=0;k<BB_MACHINES;k++)
                if(bb_mix_control(BB_MIX_KITS_PAGE,k))active+=bb_face.requested.drums[k].enabled!=0;
            cv_text(9,87,&AF_S,"LAYERS",BB_DIM);bb_number(65,83,active,&AF_M,BB_TEXT);
            cv_text(82,87,&AF_S,bb_mix_control(BB_MIX_KITS_PAGE,2)?"OF 4 ENABLED":"OF 2 ENABLED",BB_DIM);
        } else {
#if BB_SAMPLE_HOST_INTEGRATION && BB_WITH_EFFECTS
            if(bb_send_page&&sample_send_route)cv_text(9,87,&AF_S,"WHOLE SAMPLE LAYER",BB_DIM);
            else {
#else
            {
#endif
            cv_text(9,87,&AF_S,"VOICE",BB_DIM);
#if BB_WITH_RICH_DRUM_BROWSER
            if(bb_rich_step_face())cv_text(54,83,&AF_M,bb_rich_names[bb_rich_voices[bb_face.machine]],BB_TEXT);
            else
#endif
            cv_text(54,83,&AF_M,bb_ui_voice_name(),BB_TEXT);
            }
        }
        cv_text(9,105,&AF_S,bb_send_page?"PRESETS: SOUND / ALGO: PATTERN":bb_hint_until?bb_hint:
            bb_kit_page?(bb_sound_picker_enabled?"PRESETS: KIT / ALGO: PATTERN":"SELECT: KIT / ALGO: LAYER"):
            bb_group_knobs_enabled(0)?"BLACK 1-4: GROUP MUTES":"SELECT: INSTR / PRESETS: KITS",BB_DIM);
    }
    cv_blit(0,0);
    bb_backdrop(120,120,0);
    if(bb_diag_page) {
#ifdef BB_DIAGNOSTICS_WHOLE_HALF
        uint32_t total,budget,max_ticks,over,late,faults;
        const char *const labels[5]={"HALF LOAD %","MAX TOTAL US","OVER BUDGET","DMA LATE","DSP FAULTS"};
        unsigned values[5],row;
        uintptr_t token=bb_target_enter(0);
        total=bb_target_timing.last_total_ticks;budget=bb_target_timing.budget_ticks;
        max_ticks=bb_target_timing.max_total_ticks;over=bb_target_timing.budget_exceeds;
        late=bb_late_this_boot();faults=bb_audio_faults;
        bb_target_leave(0,token);
        values[0]=bb_deadline_percent(total,budget);
        values[1]=max_ticks/FM1_TICKS_PER_US;
        values[2]=over;values[3]=late;values[4]=faults;
        /* Total includes nested scan/USB work. Last-half percentage is not
         * averaged CPU or proof of remaining full-ensemble headroom. */
        cv_text(9,0,&AF_M,"AUDIO / FX:BACK",BB_TEXT);
        for(row=0;row<5;row++) {
            cv_text(9,25+(int)row*19,&AF_S,labels[row],BB_DIM);
            if(!row && !budget)cv_text(176,22,&AF_M,"--",BB_DIM);
            else bb_number(176,22+(int)row*19,
                values[row]>999999u?999999u:values[row],&AF_M,BB_TEXT);
        }
#else
        uint32_t late=bb_late_this_boot();
        /* Existing driver measurement excludes nested scan/USB time. This is
         * audio-ISR load, NOT whole-system utilization or remaining headroom.
         * Cumulative deadline misses and maximum since boot reveal transients. */
        cv_text(9,3,&AF_M,"AUDIO DIAGNOSTICS",BB_TEXT);
        cv_text(9,26,&AF_S,"RENDER LOAD %",BB_DIM);
        bb_number(176,23,(unsigned)((BB_METER_CPU_Q8*100u+128u)/256u),&AF_M,BB_TEXT);
        cv_text(9,45,&AF_S,"MAX AUDIO US",BB_DIM);
        bb_number(176,42,(unsigned)(BB_METER_MAX_US>999999u?999999u:BB_METER_MAX_US),&AF_M,BB_TEXT);
        cv_text(9,64,&AF_S,"DMA LATE",BB_DIM);
        bb_number(176,61,(unsigned)(late>999999u?999999u:late),&AF_M,BB_TEXT);
        cv_text(9,83,&AF_S,"DSP FAULTS",BB_DIM);
        bb_number(176,80,(unsigned)(bb_audio_faults>999999u?999999u:bb_audio_faults),&AF_M,BB_TEXT);
        cv_text(9,104,&AF_S,"SINCE BOOT / FX: RETURN",BB_DIM);
#endif
        cv_blit(0,120);return;
    }
#if BB_WITH_EFFECTS
    if(bb_effect_page) {
        static const char *const titles[4]={"ECHO / THREE HEADS","ECHO / COLOR","SPRING / TANK","MOTION / HEAD TRAVEL"};
        static const char *const heads[8]={"OFF","1","2","1+2","3","1+3","2+3","1+2+3"};
        cv_text(9,5,&AF_M,titles[bb_effect_bank],BB_TEXT);
        for(k=0;k<4;k++) {
            int x=9+(int)k*58;unsigned p=bb_effect_knob_parameter(k);
            const bb_knob_descriptor *control=bb_fx_knob(bb_effect_bank,k);
            if(!control)continue;
            cv_text(x,36,&AF_S,bb_nav_knob_label(k),BB_DIM);
            if(p==BB_FX_ECHO_HEADS)cv_text(x,57,&AF_S,heads[bb_fx_parameter_get(&bb_face.requested,p)],BB_TEXT);
            else bb_number(x,53,bb_fx_parameter_get(&bb_face.requested,p),&AF_M,BB_TEXT);
        }
        cv_text(9,82,&AF_S,"PRESETS: KIT / ALGO: PATTERN",BB_DIM);
        cv_text(9,105,&AF_S,"EDIT: NEXT BANK / REC+FX: THROW",BB_DIM);
        cv_blit(0,120);return;
    }
    if(bb_send_page) {
#if BB_SAMPLE_HOST_INTEGRATION
        if(sample_send_route){bb_sample_sends_draw(&sample_send_view,sample_send_route);cv_blit(0,120);return;}
#endif
        static const char *const labels[4]={"ECHO","SPRING","LEVEL","DRIVE"};
        static const uint8_t parameters[4]={BB_DRUM_ECHO,BB_DRUM_SPRING,BB_DRUM_LEVEL,BB_DRUM_DRIVE};
        unsigned synth=bb_selected_synth(),drums=bb_face.requested.page==BB_DRUMS;
        cv_text(9,5,&AF_M,drums?"VOICE / SENDS":"SYNTH / SENDS",BB_TEXT);
        for(k=0;k<(drums?4u:2u);k++) {
            int x=9+(int)k*58;
            unsigned value=drums?bb_visible_drum_value(&m->sound,parameters[k]):
                bb_fx_parameter_get(&bb_face.requested,(synth?BB_FX_TELEGRAM_ECHO:BB_FX_DILLO_ECHO)+k);
            cv_text(x,36,&AF_S,labels[k],BB_DIM);
            if(drums && k==2)bb_level_number(x,53,value,&AF_M,BB_TEXT);
            else bb_number(x,53,value,&AF_M,BB_TEXT);
        }
        cv_text(9,82,&AF_S,drums?"REC+PRESET: VOICE / POST FADER":"SENDS KEEP YOUR PATCH",BB_DIM);
        cv_text(9,105,&AF_S,"EDIT: NEXT BANK / HOME: STUDIO",BB_DIM);
        cv_blit(0,120);return;
    }
#endif
    if(bb_mix_page) {
        int band=bb_mix_page!=BB_MIX_KITS_PAGE;
        cv_text(9,5,&AF_M,band?"THE BAND":"KIT LAYERS",BB_TEXT);
        for(k=0;k<BB_MACHINES;k++) {
            const bb_mixer_descriptor *control=bb_mix_control(bb_mix_page,k);
            if(!control)continue;
            int tempo=control->kind==BB_MX_TEMPO;
            int x=8+(int)k*58,enabled=1;
            unsigned value=bb_mix_value(control);
            const char *label=control->label;
            bb_number(x,29,k+1u,&AF_S,BB_ORANGE);
            bb_rotary(x+26,57,tempo?(bb_face.requested.bpm-30u)*127u/270u:value,enabled);
            cv_text(x+26-text_w(&AF_S,label)/2,80,&AF_S,label,enabled?BB_TEXT:BB_DIM);
            if(tempo)bb_number(x+15,96,bb_face.requested.bpm,&AF_S,BB_TEXT);
            else if(enabled)bb_level_number(x+15,96,value,&AF_S,BB_TEXT);
            else cv_text(x+26-text_w(&AF_S,"UNBUILT")/2,96,&AF_S,"UNBUILT",BB_DIM);
        }
        cv_blit(0,120);
        return;
    }
    if(bb_face.requested.page!=BB_DRUMS){bb_synth_draw();cv_blit(0,120);return;}
    if(bb_kit_page) {
        bb_kit_shelf_draw();
        cv_blit(0,120);return;
    }
    if(bb_drum_sound_page && bb_kit_family[bb_face.machine]==1) {
        cv_text(9,5,&AF_M,bb_ui_voice_name(),BB_TEXT);
        for(k=0;k<4;k++) {
            int x=9+(int)k*58;unsigned p=bb_ui_909_parameter(k);
            if(bb909_control_max(bb_face.voice,p)<0)continue;
            unsigned value=bb_c1_909_controls.value[bb_face.voice][p];
            cv_text(x,36,&AF_S,bb909_control_name(bb_face.voice,p),BB_DIM);
            const char *name=bb909_control_value_name(bb_face.voice,p,value);
            if(name)cv_text(x,56,&AF_S,name,BB_TEXT);else bb_number(x,53,value,&AF_M,BB_TEXT);
        }
        cv_text(9,82,&AF_S,"REC+PRESETS: VOICE",BB_DIM);
        cv_text(9,105,&AF_S,bb_hint_until?bb_hint:"EDIT: NEXT PAGE / HOME: STUDIO",BB_DIM);
        cv_blit(0,120);return;
    }
    if(bb_drum_sound_page) {
        cv_text(9,5,&AF_M,bb_drum_color_page?"VOICE / COLOUR":"VOICE / BASIC",BB_TEXT);
        for(k=0;k<4;k++) {
            /* Navigation v2.1: the labels and values of bb_nav's roles. */
            int x=9+(int)k*58;char value[16];
            (void)bb_nav_knob_text(k,value);
            cv_text(x,36,&AF_S,bb_nav_knob_label(k),BB_DIM);
            cv_text(x,53,&AF_M,value,BB_TEXT);
        }
        cv_text(9,82,&AF_S,"WHITE 1-11 / REC+PRESETS: VOICE",BB_DIM);
        cv_text(9,105,&AF_S,bb_hint_until?bb_hint:bb_drum_color_page?"EDIT: BASIC / HOME: STUDIO":"EDIT: COLOUR / HOME: STUDIO",BB_DIM);
        cv_blit(0,120);return;
    }
    if(bb_pad_page) {
        static const char *const names[3][11]={
            {"KICK","SNARE","L TOM","M TOM","H TOM","RIM","CLAP","COW","CYM","O HAT","C HAT"},
            {"KICK","SNARE","L CON","M CON","H CON","CLAVE","MARAC","COW","CYM","O HAT","C HAT"},
            {"KICK","SNARE","L TOM","M TOM","H TOM","RIM","CLAP","C HAT","O HAT","CRASH","RIDE"}};
        int groups=bb_group_knobs_enabled(0);   /* nav: K1-K4 are groups with REC too */
        if(groups) {
            static const char *const group_labels[4]={"KICK","SN/CL","HATS","PERC"};
            bb_group_view view;
            int ready=bb_group_input(&bb_sound.commands,bb_sound.groups,bb_face.machine,
                BB_GROUP_READ,0,0,&view)!=BB_GROUP_REFUSED;
            if(ready && view.pending)bb_dirty=1;
            for(k=0;k<GG_COUNT;k++) {
                int x=11+(int)k*41;
                cv_rect(x-2,1,40,14,BB_BG);cv_text(x,2,&AF_S,group_labels[k],BB_DIM);
                /* Pending positions are deliberately not presented as applied.
                 * A muted dial retains its stored position, with MUTED below. */
                int applied=ready && !(view.pending&(1u<<k));
                bb_rotary(x+20,35,applied?view.level[k]:0,applied);
                cv_rect(x-2,57,40,20,BB_BG);
                if(!ready)cv_text(x,59,&AF_S,"BUSY",BB_DIM);
                else if(view.pending&(1u<<k))cv_text(x,59,&AF_S,"QUEUED",BB_ORANGE);
                else if(view.muted&(1u<<k))cv_text(x,59,&AF_S,"MUTED",BB_DIM);
                else bb_level_number(x,59,view.level[k],&AF_S,BB_TEXT);
            }
        }
        for(k=0;k<BB_DRUM_TRACKS;k++) {
            int x=8+(int)(k%(groups?6u:4u))*(groups?37:58);
            int y=groups?78+(int)(k/6u)*21:5+(int)(k/4u)*23;
            int width=groups?35:54;
            uint16_t ink=k==bb_face.voice?BB_ORANGE:BB_TEXT;
            unsigned family=bb_kit_family[bb_face.machine]==1?2u:(m->sound.mode[k]!=0);
            const char *name=names[family][k];
            cv_rect(x,y,width,20,BB_BG);cv_frame(x,y,width,20,ink);
            cv_text(x+(width-text_w(&AF_S,name))/2,y+6,&AF_S,name,ink);
        }
        if(groups){cv_blit(0,120);return;}
        static const char *const labels[4]={"LEVEL","TUNE","DECAY","SWING"};
        unsigned values[4]={bb_face.requested.band_level[BB_MIX_DRUMS],
            bb_drum_parameter_get(&m->sound,bb_face.voice,BB_DRUM_TUNE),
            bb_drum_parameter_get(&m->sound,bb_face.voice,BB_DRUM_DECAY),bb_face.requested.swing};
        for(k=0;k<4;k++) {
            int x=9+(int)k*58;cv_text(x,78,&AF_S,labels[k],BB_DIM);
            if(!k)bb_level_number(x,97,values[k],&AF_M,BB_TEXT);
            else if(k==3)bb_swing_number(x,97,values[k],&AF_M,BB_TEXT);
            else bb_number(x,97,values[k],&AF_M,BB_TEXT);
        }
        cv_blit(0,120);return;
    }
#if BB_WITH_RICH_DRUM_BROWSER
    if(bb_rich_active()) {
        for(k=0;k<16;k++) {
            unsigned step=bb_face.bank*16u+k;int x=8+(int)k*14;bb_phrase_cell cell={0};
            int valid=bb_rich_cell(step,&cell);
            uint16_t ink=!valid?BB_ART:cell.accents?BB_TEXT:cell.soft==cell.hits&&cell.hits?BB_DIM:BB_ORANGE;
            if(cell.hits)cv_rect(x,11,11,22,ink);else cv_frame(x,11,11,22,valid?BB_DIM:BB_ART);
            if(cell.hits>9)cv_text(x+2,14,&AF_S,"+",BB_BG);
            else if(cell.hits>1)bb_number(x+2,14,cell.hits,&AF_S,BB_BG);
            if(cell.flams)cv_rect(x,36,11,3,RGB(60,150,255));
            if(cell.repeats)cv_rect(x,41,11,3,BB_GREEN);
            if(bb_face.requested.running&&valid&&step==head)cv_rect(x,3,11,4,BB_GREEN);
            if(!(k&3u))bb_number(x,47,step+1,&AF_S,BB_DIM);
        }
        bb_phrase_view *view=&bb_rich_views[bb_face.machine];
        cv_text(9,64,&AF_S,bb_rich_names[bb_rich_voices[bb_face.machine]],BB_TEXT);
        bb_number(9,79,view->phrase.length,&AF_S,BB_TEXT);cv_text(28,79,&AF_S,"STEPS",BB_DIM);
        cv_text(87,79,&AF_S,view->phrase.lattice==BB_FDL_TRIPLETS?"TRIPLETS":view->phrase.lattice==BB_FDL_EIGHTHS?"EIGHTHS":"SIXTEENTHS",BB_TEXT);
        cv_text(9,93,&AF_S,"BLUE FLAM / GREEN REPEAT",BB_DIM);
        cv_text(9,105,&AF_S,bb_rich_tones[bb_face.machine].pending?"TONE QUEUED / WAIT":bb_hint_until?bb_hint:"REC+PRESETS: VOICE / OCT: BANK",BB_DIM);
        cv_blit(0,120);return;
    }
#endif
    for(k=0;k<16;k++) {
        unsigned step=bb_face.bank*16u+k;int x=8+(int)k*14;
        uint16_t ink=step<m->length?BB_DIM:BB_ART,bit=(uint16_t)(1u<<bb_face.voice);
        int on=step<m->length && (m->steps[step].hits&bit);
        if(on)ink=(m->steps[step].accents&bit)?BB_TEXT:BB_ORANGE;
        if(on)cv_rect(x,11,11,25,ink);else cv_frame(x,11,11,25,ink);
        if(bb_face.requested.running && step==head)cv_rect(x,3,11,4,BB_GREEN);
        if(!(k&3u))bb_number(x,40,step+1,&AF_S,BB_DIM);
    }
    bb_nav_knob_strip(62,77,&AF_M);   /* navigation v2.1: K1-K4 = groups */
    cv_text(9,105,&AF_S,bb_hint_until?bb_hint:"REC: ACCENT / BLACK 1-8: VOICE",BB_DIM);
    cv_blit(0,120);
}
/* Navigation v2.1: every LED follows bb_nav_leds() (bb_nav_leds_out),
 * replacing the per-face LED code. */
static void bb_target_leds(void) {
    if(fm1_led_anim_on())return;
    bb_nav_leds_out();
}
static void bb_target_splash(void) {
    bb_backdrop(0,120,2);cv_text(8,12,&AF_L,"BIG BEAT",BB_TEXT);cv_blit(0,0);
    bb_backdrop(120,120,2);cv_text(9,75,&AF_S,"HOLD HOME: CONTROL GUIDE",BB_TEXT);
    cv_text(9,96,&AF_S,"FM-1 / PROTOTYPE 01",BB_TEXT);cv_blit(0,120);
}
#include "bb_snapshot_startup_probe.inc"
