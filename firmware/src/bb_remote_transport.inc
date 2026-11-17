/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_remote_transport.h"
#include "bb_remote_library.h"
static bb_remote_lease bb_remote;
static int bb_remote_owned(void) {
    return bb_asset_hold_request || bb_session_hold_request || bb_kit_hold_request;
}
static uint32_t bb_remote_epoch(void) {
    return usb.resets+usb.suspends+usb.frame_stalls;
}
static int bb_remote_connected(void) {
    return cdc.dtr && usb.config && !usb.suspended;
}
static int bb_remote_send(unsigned run) {return bb_send(BB_CMD_TRANSPORT,run);}
static int bb_remote_idle(void) {
    return bb_remote_connected() && !song.master_q12 && !uac.alt &&
        !bb_remote_owned() && !bb_session_busy() && bb_target_audio_operable() &&
        bb_c1_cpu0_available() && !bb_sound.core.running &&
        !bb_face.requested.running && bb_commands_idle(&bb_sound.commands);
}
static int bb_remote_library_safe(void) {
    if(!bb_remote_connected() || song.master_q12 || uac.alt ||
       bb_remote_owned() || bb_session_busy() || !bb_target_audio_operable() ||
       !bb_c1_cpu0_available() || !bb_commands_idle(&bb_sound.commands))return 0;
    if(!bb_remote.active)return !bb_sound.core.running && !bb_face.requested.running;
    return bb_remote.active==1 && bb_remote.epoch==bb_remote_epoch() &&
           (int32_t)(fm1_ms-bb_remote.deadline)<0;
}
static void bb_remote_library_status(unsigned layer) {
    bb_library_sync();
    const bb_player *player=&bb_drum_library[layer];
    con_puts("bbtest library layer=");con_hex(layer,1);
    con_puts(" ready=");con_hex(player->ready,1);
    con_puts(" request=");con_hex(player->request.ticket,8);
    con_puts(" applied=");con_hex(player->status.applied,8);
    con_puts(" pending=");con_hex(bb_player_pending(player),1);
    con_puts(" error=");con_hex(player->error,2);
    con_puts(" result=");con_hex(player->status.result,2);
    con_puts(" playing=");con_hex(player->playing.ticket,8);
    con_puts(" kit-valid=");con_hex(player->playing.has_kit,1);
    con_puts(" kit=");con_hex(player->playing.kit,2);
    con_puts(" group=");con_hex(player->playing.choice.group,1);
    con_puts(" bank=");con_hex(player->playing.choice.bank_index,4);
    con_puts(" part=");con_hex(player->playing.choice.part_index,4);
    con_puts("\r\n");
}
#include "bb_remote_effects.inc"
static int bb_remote_command(const char *p) {
    if(bb_remote_effects_command(p))return 1;
    bb_remote_selection selection;
    if(str_eq(p,"bbtest library 0") || str_eq(p,"bbtest library 1")) {
        bb_remote_library_status(str_eq(p,"bbtest library 1"));return 1;
    }
    if(bb_remote_selection_parse(p,&selection)) {
        uintptr_t token=bb_target_enter(0);
        int safe=bb_remote_library_safe();
        bb_target_leave(0,token);
        if(!safe) {con_puts("bbtest refused\r\n");return 1;}
        bb_library_sync();
        bb_player *player=&bb_drum_library[selection.layer];
        if(!player->ready || player->target!=selection.layer ||
           !player->is_drum || bb_player_pending(player) ||
           (selection.kit && selection.kit>bb_sample_ui_count)) {
            con_puts("bbtest refused\r\n");return 1;
        }
        bb_browser next=player->view;
        if(!bb_browser_group(&next,BB_BROWSER_SONGS) ||
           selection.bank>=bb_browser_bank_count(&next) ||
           !bb_browser_move_bank(&next,(int)selection.bank-(int)next.bank_index) ||
           selection.part>=bb_browser_part_count(&next) ||
           !bb_browser_move_part_page(&next,(int)(selection.part/BB_BROWSER_PART_KEYS)-(int)next.part_page) ||
           !bb_browser_part_key(&next,selection.part%BB_BROWSER_PART_KEYS,0)) {
            con_puts("bbtest selection invalid\r\n");return 1;
        }
        bb_browser old=player->view;
        player->view=next;
        unsigned kit=selection.kit?selection.kit-1:BB_SAMPLE_808_ID;
        int result=bb_player_confirm_with_kit(player,&bb_sound,kit);
        if(result==BB_FACTORY_PATTERN_OK) {
            bb_kit_intent[selection.layer].valid=0;bb_dirty=1;
            con_puts("bbtest selection accepted; ticket=");con_hex(player->request.ticket,8);
        } else {
            player->view=old;
            con_puts("bbtest selection refused; result=");con_hex((unsigned)result,2);
        }
        con_puts("\r\n");return 1;
    }
#if BB_WITH_TELEGRAM
    /* Dillo Duo lead (T4): load a song section's lead line on the lead player, and mute/unmute the lead. */
    if(str_eq(p,"bbtest lead on") || str_eq(p,"bbtest lead off")) {
        uintptr_t token=bb_target_enter(0);
        int safe=!bb_remote.active && bb_remote_idle();
        bb_target_leave(0,token);
        int accepted=safe && bb_send_synth(1,BB_CMD_SYNTH_MUTE,str_eq(p,"bbtest lead off"),0);
        con_puts(accepted?"bbtest lead queued\r\n":"bbtest refused\r\n");
        return 1;
    }
    {
        static const char prefix[]="bbtest lead song ";
        unsigned i=0,v[2]={0,0},n=0;
        while(prefix[i]&&p[i]==prefix[i])i++;
        if(!prefix[i]) {
            const char *t=p+i;
            for(n=0;n<2;n++) {
                unsigned d=0;
                while(*t>='0'&&*t<='9'&&d<5){v[n]=v[n]*10u+(unsigned)(*t-'0');t++;d++;}
                if(!d||(n==0&&*t!=' ')||(n==1&&*t)){con_puts("bbtest selection invalid\r\n");return 1;}
                if(n==0)t++;
            }
            uintptr_t token=bb_target_enter(0);
            int safe=bb_remote_library_safe();
            bb_target_leave(0,token);
            if(!safe){con_puts("bbtest refused\r\n");return 1;}
            bb_library_sync();
            bb_player *player=&bb_library[1];
            bb_browser next=player->view;
            if(!player->ready||player->is_drum||bb_player_pending(player)||!bb_browser_group(&next,BB_BROWSER_SONGS)||
               v[0]>=bb_browser_bank_count(&next)||!bb_browser_move_bank(&next,(int)v[0]-(int)next.bank_index)||
               v[1]>=bb_browser_part_count(&next)||
               !bb_browser_move_part_page(&next,(int)(v[1]/BB_BROWSER_PART_KEYS)-(int)next.part_page)||
               !bb_browser_part_key(&next,v[1]%BB_BROWSER_PART_KEYS,0)){con_puts("bbtest selection invalid\r\n");return 1;}
            bb_browser old=player->view;
            player->view=next;
            int result=bb_library_confirm(player);
            if(result==BB_FACTORY_PATTERN_OK){bb_dirty=1;con_puts("bbtest lead selection accepted; ticket=");con_hex(player->request.ticket,8);}
            else {player->view=old;con_puts("bbtest lead selection refused; result=");con_hex((unsigned)result,2);}
            con_puts("\r\n");return 1;
        }
    }
#endif
    if(str_eq(p,"bbtest drum-b on") || str_eq(p,"bbtest drum-b off")) {
        uintptr_t token=bb_target_enter(0);
        int safe=!bb_remote.active && bb_remote_idle();
        bb_target_leave(0,token);
        int accepted=safe && bb_send_machine(1,BB_CMD_DRUM_ENABLE,str_eq(p,"bbtest drum-b on"));
        con_puts(accepted?"bbtest drum-b queued\r\n":"bbtest refused\r\n");
        return 1;
    }
    if(str_eq(p,"bbtest start")) {
        uintptr_t token=bb_target_enter(0);
        int safe=bb_remote_idle();
        bb_target_leave(0,token);
        int accepted=bb_remote_start(&bb_remote,fm1_ms,bb_remote_epoch(),safe,bb_remote_send);
        con_puts(accepted?"bbtest queued; limit 30s\r\n":"bbtest refused\r\n");
        return 1;
    }
    if(str_eq(p,"bbtest stop")) {
        bb_remote.active=2;
        con_puts("bbtest stop requested\r\n");return 1;
    }
    return 0;
}
static void bb_remote_service(void) {
    bb_remote_poll(&bb_remote,fm1_ms,bb_remote_epoch(),bb_remote_connected(),
        !song.master_q12 && !uac.alt && bb_target_audio_operable() && bb_c1_cpu0_available() &&
        !bb_remote_owned(),bb_remote_owned(),bb_remote_send);
}
