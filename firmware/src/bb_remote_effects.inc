/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_remote_effects.h"
static int bb_remote_effects_command(const char *p) {
    unsigned v[3];
    if(bb_remote_numbers(p,"bbtest fx ",2,v)) {
        unsigned lo,hi;
        if(v[0]>=BB_FX_PARAMETER_COUNT || v[0]==BB_FX_TELEGRAM_ECHO ||
           v[0]==BB_FX_TELEGRAM_SPRING){con_puts("bbtest refused\r\n");return 1;}
        bb_fx_parameter_limits(v[0],&lo,&hi);
        uintptr_t token=bb_target_enter(0);int safe=bb_remote_library_safe();bb_target_leave(0,token);
        int accepted=safe && v[1]>=lo && v[1]<=hi && bb_send_effect_parameter(v[0],v[1]);
        con_puts(accepted?"bbtest fx queued\r\n":"bbtest refused\r\n");return 1;
    }
    if(bb_remote_numbers(p,"bbtest send ",3,v)) {
        uintptr_t token=bb_target_enter(0);int safe=bb_remote_library_safe();bb_target_leave(0,token);
        bb_sample_status view;bb_command command;
        int accepted=safe && v[0]<2 && v[1]<2 && v[2]<=127 &&
            bb_audio_sample_status_layer(&bb_sound,v[0],&view) &&
            view.selected<view.count && view.queue_idle && !view.stop_pending &&
            bb_audio_sample_mix_command(&view,v[1],v[2],&command) &&
            bb_command_push(&bb_sound.commands,&command);
        if(accepted)bb_dirty=1;
        con_puts(accepted?"bbtest send queued\r\n":"bbtest refused\r\n");return 1;
    }
    if(str_eq(p,"bbtest effects")) {
        uint16_t values[BB_FX_PARAMETER_COUNT];
        uintptr_t token=bb_target_enter(0);
        int safe=!bb_remote_owned()&&!bb_session_busy();
        if(safe)for(unsigned i=0;i<BB_FX_PARAMETER_COUNT;i++)values[i]=bb_sound.core.effects.value[i];
        bb_target_leave(0,token);
        if(!safe){con_puts("bbtest refused\r\n");return 1;}
        for(unsigned i=0;i<BB_FX_PARAMETER_COUNT;i++) {
            con_puts("bbtest fx applied id=");con_hex(i,2);
            con_puts(" value=");con_hex(values[i],4);con_puts("\r\n");
        }
        for(unsigned i=0;i<2;i++) {
            bb_sample_status view;
            if(!bb_audio_sample_status_layer(&bb_sound,i,&view))continue;
            con_puts("bbtest sends layer=");con_hex(i,1);
            con_puts(" kit=");con_hex(view.selected,2);
            con_puts(" echo=");con_hex(view.echo,2);
            con_puts(" spring=");con_hex(view.spring,2);con_puts("\r\n");
        }
        return 1;
    }
    return 0;
}
