/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop-only consumer of Felucca's USB/TRS packet ring. No DSP/ISR writes,
 * heap work, MIDI output or SysEx/OTA handling. Source ownership is audio-owned.
 * Include after bb_target_audio/session and before bb_target_ui. */
#define BB_MIDI_INPUT 1
#define BB_MIDI_POLL_BUDGET 8u
/* Native unity already supplies its unsigned-length libc definitions. */
#define BB_MIDI_COMPAT_LIBC_PROVIDED 1
#include "bb/bb_midi_compat.h"
#undef BB_MIDI_COMPAT_LIBC_PROVIDED
#include "bb_midi_globals.inc"
static bb_midi_compat_state bb_midi_compat;
/* Optional main-loop program Start; NULL keeps ordinary1.5 MIDI semantics. */
static int (*bb_midi_program_start)(void);
static struct {
    uint32_t epoch,drop_usb_until;
    uint8_t initialized,ready,pending,discard_all,blocked,drop_usb;
} bb_midi;

#if BB_WITH_PROJECT_FILES
/* Main owner already excludes IRQ producers. Publish the exact held-input
 * boundary without touching the paused/rebuilt sound actor or its queue.
 * Clearing blocked/discard_all preserves packets arriving AFTER release. */
static void bb_midi_project_release_owned(void){
    mi_r=mi_w;midi_in_overflow=0;
    bb_midi_compat_reset(&bb_midi_compat);
    bb_midi.pending|=15u;
    bb_midi.blocked=bb_midi.discard_all=0;
    RING_PUBLISH();
}
#endif

/* The parser stamps each USB packet with this same connection generation.
 * Unsigned sum is intentional; wrap remains comparable within a 64-slot ring. */
static uint32_t bb_midi_epoch(void) {
    return usb.resets+usb.suspends+usb.frame_stalls;
}
static void bb_midi_reset(void) {
    uintptr_t token=bb_target_enter(0);
    memset(&bb_midi,0,sizeof bb_midi);
    bb_midi_compat_reset(&bb_midi_compat);
    bb_midi.epoch=bb_midi_epoch();
    bb_midi.ready=(uint8_t)(usb.config && !usb.suspended);
    bb_midi.initialized=1;
    mi_r=mi_w;midi_in_overflow=0;
    /* Boot/reset is stopped before physical audio; restore persisted OFF/ON
     * without creating a transport event. No automatic note/clock THRU. */
    if(!bb_sound.core.running&&bb_commands_idle(&bb_sound.commands))
        bb_sound.midi_output.enabled=bb_midi_compat_config.clock_out;
    RING_PUBLISH();bb_target_leave(0,token);
}
static int bb_midi_configure(const bb_midi_compat_settings *c){
    if(!bb_midi_compat_settings_valid(c))return 0;
    if(!memcmp(c,&bb_midi_compat_config,sizeof *c))return 1;
    int channels=memcmp(c->drum_channel,bb_midi_compat_config.drum_channel,2)||
                 memcmp(c->synth_channel,bb_midi_compat_config.synth_channel,2);
    uintptr_t token=bb_target_enter(0);
    if(c->clock_out!=bb_midi_compat_config.clock_out){
        /* Quiescent ownership transfer: CPU0 audio excluded, stopped, no
         * pending owner commands. Do not flip output behind a running owner. */
        if(bb_sound.core.running||!bb_commands_idle(&bb_sound.commands)){
            bb_target_leave(0,token);return 0;
        }
        bb_sound.midi_output.enabled=c->clock_out;
        bb_sound.midi_output.phase=0;bb_sound.midi_output.running=0;
    }
    if(channels){
        mi_r=mi_w;midi_in_overflow=0;RING_PUBLISH();bb_midi.pending|=15u;
        bb_midi_compat_reset(&bb_midi_compat);
    }else bb_midi_compat_clock_reset(&bb_midi_compat);
    bb_midi_compat_config=*c;
    bb_target_leave(0,token);return 1;
}
/* Configured explicit synth channels, from either musical input source.
 * Unimplemented messages do not masquerade as controls: no sustain/bend/PC.
 * CC123 is source-local All Notes Off, with the instrument's natural release. */
static int bb_midi_decode(uint32_t packet,unsigned source,bb_command *command) {
    unsigned cin=packet&15u,status=(packet>>8)&255u;
    unsigned channel=status&15u,note=(packet>>16)&255u,velocity=packet>>24;
    if((packet&0xf0u) || (source!=BB_KEY_USB && source!=BB_KEY_TRS) ||
       note>127 || velocity>127 || (status>>4)!=cin ||
       !bb_midi_compat_settings_valid(&bb_midi_compat_config))return 0;
    unsigned target;
    for(target=0;target<2;target++)if(bb_midi_compat_config.synth_channel[target]==channel+1u&&bb_midi_compat_synth(target))break;
    if(target==2)return 0;
    if(cin!=8 && cin!=9)return 0;
    memset(command,0,sizeof *command);
    command->target=(uint8_t)target;command->step=(uint8_t)source;
    if(cin==11)command->type=BB_CMD_SYNTH_SOURCE_RELEASE;
    else {
        command->type=BB_CMD_SYNTH_KEY;command->voice=(uint8_t)note;
        command->value=(uint16_t)(cin==9?velocity:0);
    }
    return 1;
}
static int bb_midi_release_pending(void) {
    for(unsigned i=0;i<4;i++)if(bb_midi.pending&(1u<<i)) {
        /* A compiled-out synth owns no notes. Do not let its rejected release
         * prevent the enabled synth or USB packet ring from progressing. */
        unsigned target=i%2u;
        if((target==0 && !BB_WITH_DILLO) || (target==1 && !BB_WITH_TELEGRAM)) {
            bb_midi.pending&=(uint8_t)~(1u<<i);continue;
        }
        bb_command c={0};c.type=BB_CMD_SYNTH_SOURCE_RELEASE;
        c.target=(uint8_t)(i%2u);c.step=(uint8_t)(1u+i/2u);
        if(!bb_command_push(&bb_sound.commands,&c))return 0;
        bb_midi.pending&=(uint8_t)~(1u<<i);
    }
    return 1;
}
/* paused means the scene system owns/resetting bb_sound: NO queue access.
 * suppressed means recovery/UI asks not to admit notes; release remains safe.
 * Each call takes only its entry packet snapshot, capped to eight packets. */
static void bb_midi_input(int paused,int suppressed) {
    uint32_t end,epoch;unsigned ready,overflow,live_bpm;
    uintptr_t token;
    if(!bb_midi.initialized)bb_midi_reset();
    token=bb_target_enter(0);
    end=mi_w;epoch=bb_midi_epoch();ready=usb.config && !usb.suspended;
    overflow=midi_in_overflow;live_bpm=bb_sound.core.bpm;
    if(paused || suppressed) {
        mi_r=end;midi_in_overflow=0;RING_PUBLISH();
    }
    bb_target_leave(0,token);
    /* A local edit or song load must not defeat an ongoing clock source.
     * Invalidate the cached estimate; only subsequent incoming clocks can
     * reassert tempo, never a background write when the cable goes quiet. */
    if(!bb_midi_compat.pending&&bb_midi_compat.last_bpm&&bb_midi_compat.last_bpm!=live_bpm)bb_midi_compat.last_bpm=0;
    if(epoch!=bb_midi.epoch || (bb_midi.ready && !ready)) {
        bb_midi.pending|=3u; /* USB only; physical and TRS survive. */
        memset(bb_midi_compat.bank_msb[BB_KEY_USB],0,2);memset(bb_midi_compat.bank_lsb[BB_KEY_USB],0,2);
        if(bb_midi_compat_config.clock_source==BB_KEY_USB)bb_midi_compat_clock_reset(&bb_midi_compat);
        if(bb_midi_compat.pending&&bb_midi_compat.pending_source==BB_KEY_USB)bb_midi_compat.pending=0;
        if(bb_midi.ready && !ready) {
            bb_midi.drop_usb_until=end;bb_midi.drop_usb=1;
        }
    }
    bb_midi.epoch=epoch;bb_midi.ready=(uint8_t)ready;
    if(paused || suppressed) {
        bb_midi_compat_reset(&bb_midi_compat);
        if(!bb_midi.blocked)bb_midi.pending|=15u;
        bb_midi.blocked=1;
        if(paused)return;
        (void)bb_midi_release_pending();return;
    }
    if(bb_midi.blocked) {
        /* Flush packets accumulated between the last held poll and release;
         * neither save nor recall may replay a note that arrived while held. */
        bb_midi.discard_all=1;bb_midi.blocked=0;
    }
    /* Latch once: re-arming already queued releases on every congested poll
     * could starve the remaining sources indefinitely. */
    if(overflow && !bb_midi.discard_all){bb_midi_compat_reset(&bb_midi_compat);bb_midi.pending|=15u;bb_midi.discard_all=1;}
    if(!bb_midi_release_pending())return;
    if(bb_midi.discard_all) {
        token=bb_target_enter(0);mi_r=mi_w;midi_in_overflow=0;RING_PUBLISH();
        bb_target_leave(0,token);bb_midi.discard_all=0;return;
    }
    for(unsigned i=0;i<BB_MIDI_POLL_BUDGET;i++) {
        uint32_t position,packet,packet_epoch,packet_ms;unsigned source;
        bb_command command;
        token=bb_target_enter(0);
        position=mi_r;
        if(position==end || midi_in_overflow){bb_target_leave(0,token);return;}
        packet=midi_in_q[position%MQ];source=midi_in_source[position%MQ];
        packet_epoch=midi_in_epoch[position%MQ];packet_ms=midi_in_ms[position%MQ];
        bb_target_leave(0,token);
        if(bb_midi.drop_usb && (int32_t)(position-bb_midi.drop_usb_until)>=0)
            bb_midi.drop_usb=0;
        int stale=source==BB_KEY_USB && (!ready || packet_epoch!=epoch || bb_midi.drop_usb);
        if(!stale && bb_midi_decode(packet,source,&command)) {
            /* Reserve four slots for physical playing/controls. Releases may
             * use that reserve; a rejected packet stays at the ring head. */
            if(command.type==BB_CMD_SYNTH_KEY && command.value) {
                token=bb_target_enter(0);unsigned count=bb_sound.commands.count;
                bb_target_leave(0,token);
                if(count>=BB_COMMAND_CAPACITY-4u)return;
            }
            if(!bb_command_push(&bb_sound.commands,&command))return;
        } else if(!stale){
            int program=0;
            if(bb_midi_program_start&&!bb_midi_compat.pending){
                bb_midi_compat_state planned=bb_midi_compat;bb_command intended;
                if(bb_midi_compat_plan(&planned,&bb_midi_compat_config,packet,source,
                        (uint32_t)(packet_ms*1000u),&intended)&&intended.type==BB_CMD_TRANSPORT_RESTART){
                    program=bb_midi_program_start();
                    if(program<0)return; /* retain packet until the owner queue can accept it */
                    if(program){planned.pending=0;bb_midi_compat=planned;}
                }
            }
            if(!program&&!bb_midi_compat_dispatch(&bb_midi_compat,&bb_midi_compat_config,
                   packet,source,(uint32_t)(packet_ms*1000u),&bb_sound.commands))return;
        }
        token=bb_target_enter(0);mi_r=position+1u;RING_PUBLISH();bb_target_leave(0,token);
    }
}
