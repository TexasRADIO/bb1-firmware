/* SPDX-License-Identifier: GPL-3.0-only
 * HOST REVIEW: optional main-loop adapter to EXISTING owner commands.
 * Explicit global settings; no engine/ISR/stock-sequencer access.
 * Clock mode is integer tempo-follow, NOT external phase synchronization.
 * Configure/reset in main only; flush queued input and release old source keys.
 */
#ifndef BB_MIDI_COMPAT_H
#define BB_MIDI_COMPAT_H
#include "bb_control.h"
#ifndef BB_MIDI_COMPAT_LIBC_PROVIDED
#include <string.h>
#endif
#include "bb_midi_settings.h"
typedef struct {
    uint32_t last_us,elapsed_us;
    uint8_t primed,intervals,pending,pending_source;
    uint16_t last_bpm;
    bb_command command; /* retry exact queued command, never recalculate from retry time */
} bb_midi_compat_state;
static inline void bb_midi_compat_reset(bb_midi_compat_state *s) { memset(s,0,sizeof *s); }
static inline int bb_midi_compat_synth(unsigned n) {
    return (n==0 && BB_WITH_DILLO) || (n==1 && BB_WITH_TELEGRAM);
}
/* Exact basic GM roles supported by existing physical-lane audition command.
 * Rich percussion/ride and alternate GM articulations remain unsupported. */
static inline int bb_midi_compat_lane(unsigned note) {
    static const uint8_t notes[BB_DRUM_TRACKS]={36,38,45,47,50,37,39,56,49,46,42};
    for(unsigned i=0;i<BB_DRUM_TRACKS;i++)if(note==notes[i])return (int)i;
    return -1;
}
/* Pure plan on caller's copied state. Returns 1 iff an owner command is ready.
 * Clock timestamps must be monotonic uint32 microseconds, wrapping naturally.
 * Target integration supplies existing midi_in_ms enqueue stamps converted to us.
 * These retain queue timing, but delayed UART polling is not physical arrival time.
 * Duplicate timestamps/gaps reset measurement; no clock-loss auto-stop/fallback.
 */
static inline int bb_midi_compat_plan(bb_midi_compat_state *s,const bb_midi_compat_settings *c,
                                     uint32_t packet,unsigned source,uint32_t now,bb_command *out) {
    unsigned cin=packet&15u,status=(packet>>8)&255u,a=(packet>>16)&255u,b=packet>>24;
    if(!bb_midi_compat_settings_valid(c) || (source!=BB_KEY_USB&&source!=BB_KEY_TRS) || (packet&0xf0u))return 0;
    memset(out,0,sizeof *out);
    if(cin==15 && (status==0xf8 || status==0xfa || status==0xfc) && !a && !b) {
        if(source!=c->clock_source)return 0;
        if(status!=0xf8) {
            if(!c->transport)return 0;
            s->primed=s->intervals=0;s->elapsed_us=0;s->last_bpm=0;
            out->type=status==0xfa?BB_CMD_TRANSPORT_RESTART:BB_CMD_TRANSPORT;return 1;
        }
        if(!c->tempo_follow)return 0;
        uint32_t dt=now-s->last_us;s->last_us=now;
        if(!s->primed || dt<1000u || dt>100000u) {
            s->primed=1;s->intervals=0;s->elapsed_us=0;s->last_bpm=0;return 0;
        }
        s->elapsed_us+=dt;
        if(++s->intervals<24u)return 0;
        unsigned bpm=(60000000u+s->elapsed_us/2u)/s->elapsed_us;
        s->intervals=0;s->elapsed_us=0;
        if(bpm<30u || bpm>300u || bpm==s->last_bpm)return 0;
        s->last_bpm=(uint16_t)bpm;out->type=BB_CMD_TEMPO;out->value=(uint16_t)bpm;return 1;
    }
    if(a>127 || b>127 || (status>>4)!=cin || status>=0xf0)return 0;
    unsigned channel=(status&15u)+1u;
    for(unsigned machine=0;machine<2;machine++)if(c->drum_channel[machine] && channel==c->drum_channel[machine] && cin==9 && b) {
        int lane=bb_midi_compat_lane(a);if(lane<0)return 0;
        out->type=BB_CMD_DRUM_TRIGGER;out->target=(uint8_t)machine;
        out->voice=(uint8_t)lane;out->value=(uint16_t)b;return 1;
    }
    for(unsigned synth=0;synth<2;synth++)if(bb_midi_compat_synth(synth)&&c->synth_channel[synth] && channel==c->synth_channel[synth] && cin==11) {
        /* CC121 resets transient performance controllers only. This adapter has
         * no sustain/bend/expression state: keep volume and sound parameters,
         * held notes, transport, and clock estimator unchanged. */
        if(a==121)return 0;
        if(a==120){out->type=BB_CMD_SYNTH_SILENCE;out->target=synth;return 1;}
        if(a==7) { out->type=BB_CMD_BAND_LEVEL;out->target=(uint8_t)(BB_MIX_DILLO+synth);out->value=(uint16_t)b;return 1; }
        if(a==74 || a==71) {
            unsigned lo,hi,param=a==74?BB_PARAM_CUTOFF:BB_PARAM_RESONANCE;
            if(!bb_synth_parameter_limits(synth,param,&lo,&hi))return 0;
            out->type=BB_CMD_SYNTH_PARAMETER;out->target=synth;out->voice=(uint8_t)param;
            out->value=(uint16_t)(lo+((hi-lo)*b+63u)/127u);return 1;
        }
    }
    return 0; /* no PC, bank, bend, sustain, FB, F2 or MIDI-out equivalence */
}
/* Consume one packet or return 0 to park it at the existing ring head.
 * Non-STOP additions leave four FIFO slots for physical playing/releases.
 * STOP goes through the real queue's isolated stop lane even when FIFO full.
 */
static inline int bb_midi_compat_dispatch(bb_midi_compat_state *s,const bb_midi_compat_settings *c,
                                         uint32_t packet,unsigned source,uint32_t now,bb_commands *q) {
    bb_midi_compat_state next=*s;bb_command command;
    if(s->pending)command=s->command;
    else if(!bb_midi_compat_plan(&next,c,packet,source,now,&command)) { *s=next;return 1; }
    int stop=command.type==BB_CMD_TRANSPORT && !command.value;
    uintptr_t token=q->enter?q->enter(q->lock_context):0;
    unsigned count=q->count,stopping=q->stop_pending || q->stop_inflight;
    if(q->leave)q->leave(q->lock_context,token);
    if(command.type==BB_CMD_TRANSPORT_RESTART && stopping) {
        next.pending=0;*s=next;return 1; /* a newer owner STOP supersedes retry */
    }
    if((!stop && count>=BB_COMMAND_CAPACITY-4u) || !bb_command_push(q,&command)) {
        next.pending=1;next.pending_source=(uint8_t)source;next.command=command;*s=next;return 0;
    }
    next.pending=0;*s=next;return 1;
}
#endif
