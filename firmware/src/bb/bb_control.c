#ifndef BB_WITH_909_ROUTE
#define BB_WITH_909_ROUTE 0
#endif
#if BB_WITH_909_ROUTE
#include "bb909_controls.h"
#endif
#include "bb_control.h"
#include "bb_one_drum_profile.h"
#include "bb_transpose.h"
#include <string.h>
static uintptr_t enter(bb_commands *q) {return q->enter?q->enter(q->lock_context):0;}
static void leave(bb_commands *q,uintptr_t token) {if(q->leave)q->leave(q->lock_context,token);}
int bb_commands_init(bb_commands *q,bb_enter_fn begin,bb_leave_fn end,void *ctx) {
    if(!q || (!begin != !end))return 0;
    memset(q,0,sizeof *q);q->enter=begin;q->leave=end;q->lock_context=ctx;return 1;
}
static int valid(const bb_command *c) {
    if(!c)return 0;
    /* Reject unavailable instrument commands before they enter the queue. */
    switch(c->type) {
    case BB_CMD_TELEGRAM_PATCH:return 0;
    case BB_CMD_NOTE:case BB_CMD_NOTE_EDIT:case BB_CMD_NOTE_PITCH:case BB_CMD_NOTE_SHIFT:
    case BB_CMD_SYNTH_PATTERN:case BB_CMD_SYNTH_KEY:case BB_CMD_SYNTH_SOURCE_RELEASE:
    case BB_CMD_SYNTH_RELEASE:case BB_CMD_SYNTH_TRANSPOSE:case BB_CMD_SYNTH_MUTE:
    case BB_CMD_SYNTH_LENGTH:case BB_CMD_SYNTH_PARAMETER:
        if(c->target)return 0;break;
    case BB_CMD_BAND_LEVEL:case BB_CMD_BAND_TOGGLE:
        if(c->target==2)return 0;break;
    default:break;
    }
    /* Drum and synth target indices are separate domains. */
    switch(c->type) {
    case BB_CMD_DRUM_STEP:case BB_CMD_DRUM_LENGTH:case BB_CMD_DRUM_MUTE:
    case BB_CMD_DRUM_LEVEL:case BB_CMD_DRUM_ENABLE:case BB_CMD_DRUM_KIT:
    case BB_CMD_DRUM_PARAMETER:case BB_CMD_DRUM_TRIGGER:case BB_CMD_DRUM_TOGGLE:
    case BB_CMD_DRUM_PATTERN:case BB_CMD_GROUP_LEVEL:case BB_CMD_GROUP_MUTE:
#if BB_WITH_RICH_DRUM_HANDOFF
    case BB_CMD_DRUM_PHRASE:
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_KIT_PHRASE:
#endif
#endif
#if BB_WITH_RICH_DRUM_RENDER
    case BB_CMD_DRUM_SOURCE_PARAMETER:
#endif
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        if(c->target>=2)return 0;break;
#else
        if(c->target)return 0;break;
#endif
    default:break;
    }
    switch(c->type) {
    case BB_CMD_GROUP_LEVEL: case BB_CMD_GROUP_MUTE:
        return c->target<BB_MACHINES && c->voice<4 && !c->step
            && !c->note.note && !c->note.gate && !c->note.accent && !c->note.slide
            && c->value<=(c->type==BB_CMD_GROUP_LEVEL?127u:1u);
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_SAMPLE_MIX:
        return c->target<
#if defined(BB_SAMPLE_DUAL_TRIAL)
            2u
#else
            1u
#endif
            && c->step<8u && c->voice<BB_SAMPLE_MIX_COUNT &&
            c->value<=(c->voice==BB_SAMPLE_MIX_GAIN?100u:127u) &&
            (c->voice!=BB_SAMPLE_MIX_GAIN || c->value>=1u);
    case BB_CMD_SAMPLE_KIT:
        return (c->value<8
#if defined(BB_SAMPLE_HYBRID_TRIAL)
            ||c->value==254u /* retained808, not a sampled catalogue index */
#endif
            ) && c->target<
#if defined(BB_SAMPLE_DUAL_TRIAL)
            2u
#else
            1u
#endif
            && !c->step && !c->voice &&
            !c->note.note && !c->note.gate && !c->note.accent && !c->note.slide;
#endif
    case BB_CMD_TRIO_BANK:
#if BB_WITH_PARTS
        return c->target<8u && !c->step &&
            ((uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24)) &&
            !c->note.gate && !c->note.accent && !c->note.slide;
#else
        return 0;
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
    case BB_CMD_DRUM_PHRASE:
        return c->target<2u && c->step<BB_PATTERN_SLOTS &&
            ((uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24)) &&
            !c->note.gate && !c->note.accent && !c->note.slide;
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_KIT_PHRASE:
        return c->target<2u && c->step<BB_PATTERN_SLOTS &&
            ((uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24)) &&
            (c->note.gate<8u
#if defined(BB_SAMPLE_HYBRID_TRIAL)
             ||c->note.gate==254u
#endif
            ) &&
            !c->note.accent && !c->note.slide;
#endif
#endif
    case BB_CMD_DRUM_PATTERN:
#if BB_WITH_PATTERN
        return c->target<BB_MACHINES && c->step<BB_PATTERN_SLOTS &&
            ((uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24)) &&
            !c->note.gate && !c->note.accent && !c->note.slide;
#else
        return 0;
#endif
    case BB_CMD_SYNTH_PATTERN:
#if BB_WITH_PATTERN
        return c->target<BB_SYNTHS && c->step<BB_PATTERN_SLOTS &&
            ((uint32_t)c->value|((uint32_t)c->voice<<16)|((uint32_t)c->note.note<<24)) &&
            !c->note.gate && !c->note.accent && !c->note.slide;
#else
        return 0;
#endif
    case BB_CMD_PART_LAUNCH:
#if BB_WITH_PARTS
        return c->value<8;
#else
        return 0;
#endif
    case BB_CMD_EVOLVE:
#if BB_WITH_EVOLVE
        switch(c->voice) {
        case BB_EV_ENABLE:case BB_EV_FREEZE:return c->value<=1;
        case BB_EV_CAPTURE:case BB_EV_HOME:case BB_EV_RUN:return c->value==0;
        case BB_EV_AMOUNT:return c->value<=100;
        case BB_EV_INTERVAL:return c->value>=1 && c->value<=64;
        case BB_EV_PARTS:return c->value<64;
        case BB_EV_ATTRIBUTES:return c->value<16;
        case BB_EV_ROOT:return c->value<12;
        case BB_EV_SCALE:return c->value>=1 && c->value<4096;
        case BB_EV_LOW:case BB_EV_HIGH:return c->value<=127;
        case BB_EV_VOICES:return c->value<2048;
        case BB_EV_LOCK:return c->target<BB_MACHINES+BB_SYNTHS && c->step<BB_STEPS && c->value<=1;
        default:return 0;
        }
#else
        return 0;
#endif
    case BB_CMD_909_PARAMETER:
#if BB_WITH_909_ROUTE
        return c->target<BB_MACHINES&&bb909_control_valid(c->voice,c->step,c->value)
            &&(c->note.note||c->note.gate||c->note.accent||c->note.slide);
#else
        return 0;
#endif
    case BB_CMD_TEMPO:return c->value>=30 && c->value<=300;
    case BB_CMD_SWING:return c->value>=50 && c->value<=75;
    case BB_CMD_SWING_X2:return c->value>=100 && c->value<=150;
    case BB_CMD_TRANSPORT:return c->value<=1;
    case BB_CMD_DRUM_STEP:return c->target<BB_MACHINES && c->step<BB_STEPS && c->voice<BB_VOICES && (c->value==0 || c->value==1 || c->value==3);
    case BB_CMD_DRUM_TOGGLE:return c->target<BB_MACHINES && c->step<BB_STEPS && c->voice<BB_DRUM_TRACKS && c->value<=1;
    case BB_CMD_NOTE_EDIT:return c->target<BB_SYNTHS && c->step<BB_STEPS && c->voice<=BB_SLIDE && c->value<=127;
    case BB_CMD_NOTE_PITCH:return c->target<BB_SYNTHS && c->step<BB_STEPS && c->value<=127;
    case BB_CMD_NOTE_SHIFT:return c->target<BB_SYNTHS && c->step<BB_STEPS && c->value<=254;
    case BB_CMD_DRUM_LENGTH:return c->target<BB_MACHINES && c->value>=1 && c->value<=BB_STEPS;
    case BB_CMD_DRUM_MUTE:case BB_CMD_DRUM_ENABLE:return c->target<BB_MACHINES && c->value<=1;
    case BB_CMD_DRUM_LEVEL:return c->target<BB_MACHINES && c->value<=127;
    case BB_CMD_BAND_TOGGLE:return c->target<BB_MIX_CHANNELS && !c->value && !c->step && !c->voice && !c->note.note && !c->note.gate && !c->note.accent && !c->note.slide;
    case BB_CMD_BAND_LEVEL:return c->target<BB_MIX_CHANNELS && c->value<=127;
    case BB_CMD_DRUM_TRIGGER:return c->target<BB_MACHINES && c->voice<BB_DRUM_TRACKS && c->value>=1 && c->value<=127;
    case BB_CMD_SYNTH_KEY:return c->target<BB_SYNTHS && c->step<BB_KEY_SOURCES && c->voice<=127 && c->value<=127;
    case BB_CMD_SYNTH_SOURCE_RELEASE:return c->target<BB_SYNTHS && c->step<BB_KEY_SOURCES && c->value==0;
    case BB_CMD_SYNTH_RELEASE:return c->target<BB_SYNTHS && c->value==0;
    case BB_CMD_SYNTH_TRANSPOSE:return c->target<BB_SYNTHS && c->value<=2u*BB_TRANSPOSE_LIMIT
        && !c->step && !c->voice && !c->note.note && !c->note.gate && !c->note.accent && !c->note.slide;
    case BB_CMD_DRUM_KIT:return c->target<BB_MACHINES && c->value<BB_DRUM_KITS;
    case BB_CMD_DRUM_PARAMETER: {
        static const bb_drum_sound schema={0};
        int max=bb_drum_parameter_max(&schema,c->voice,c->step);
        return c->target<BB_MACHINES && max>=0 && c->value<=(unsigned)max;
    }
#if BB_WITH_RICH_DRUM_RENDER
    case BB_CMD_DRUM_SOURCE_PARAMETER: {
        int max=bb_drum_source_parameter_max(c->voice,c->step);
        return c->target<BB_MACHINES&&max>=0&&c->value<=(unsigned)max;
    }
#endif
    case BB_CMD_NOTE:return c->target<BB_SYNTHS && c->step<BB_STEPS && c->note.note<=127 && c->note.gate<=1 && c->note.accent<=1 && c->note.slide<=1 && (c->note.gate || (!c->note.accent && !c->note.slide));
    case BB_CMD_DILLO_PATCH:return c->value<BB_DILLO_PATCHES;
    case BB_CMD_TELEGRAM_PATCH:return c->value<BB_TELEGRAM_PATCHES;
    case BB_CMD_SYNTH_MUTE:return c->target<BB_SYNTHS && c->value<=1;
    case BB_CMD_SYNTH_LENGTH:return c->target<BB_SYNTHS && c->value>=1 && c->value<=BB_STEPS;
    case BB_CMD_SYNTH_PARAMETER: {
        unsigned lo,hi;
        return bb_synth_parameter_limits(c->target,c->voice,&lo,&hi) && c->value>=lo && c->value<=hi;
    }
    case BB_CMD_FX_PARAMETER: {
        unsigned lo,hi;
        return bb_fx_parameter_limits(c->voice,&lo,&hi) && c->value>=lo && c->value<=hi;
    }
    default:return 0;
    }
}
int bb_command_push(bb_commands *q,const bb_command *c) {
    uintptr_t token;
    if(!q || !valid(c))return 0;
    token=enter(q);
    if(c->type==BB_CMD_TRANSPORT && !c->value) {
        q->stop_pending=1;leave(q,token);return 1;
    }
    if(c->type==BB_CMD_TRANSPORT && (q->stop_pending || q->stop_inflight)) {
        leave(q,token);return 0;
    }
    if(q->count==BB_COMMAND_CAPACITY){leave(q,token);return 0;}
    q->items[q->write]=*c;q->write=(q->write+1u)%BB_COMMAND_CAPACITY;q->count++;
    leave(q,token);return 1;
}
static void apply(bb_core *core,const bb_command *c) {
    switch(c->type) {
    case BB_CMD_TEMPO:bb_set_tempo(core,c->value);break;
    case BB_CMD_SWING:bb_set_swing(core,c->value);break;
    case BB_CMD_SWING_X2:bb_set_swing_x2(core,c->value);break;
    case BB_CMD_TRANSPORT:if(c->value)bb_start(core);else bb_stop(core);break;
    case BB_CMD_DRUM_STEP: {
        bb_drum_step *step=&core->drums[c->target].steps[c->step];
        uint16_t bit=(uint16_t)(1u<<c->voice);
        step->hits=(uint16_t)((step->hits&~bit)|((c->value&1u)?bit:0));
        step->accents=(uint16_t)((step->accents&~bit)|((c->value&2u)?bit:0));break;
    }
    case BB_CMD_DRUM_LENGTH:bb_set_length(core,c->target,c->value);break;
    case BB_CMD_DRUM_TOGGLE: {
        bb_drum_step *s=&core->drums[c->target].steps[c->step];
        uint16_t bit=(uint16_t)(1u<<c->voice);
        if(c->value){if(s->hits&bit)s->accents^=bit;}
        else {s->hits^=bit;s->accents&=(uint16_t)~bit;}
        break;
    }
    case BB_CMD_NOTE_EDIT: {
        bb_note *n=&core->synths[c->target].notes[c->step];
        if(c->voice==BB_GATE) {
            n->gate^=1u;
            if(n->gate)n->note=(uint8_t)c->value;else n->accent=n->slide=0;
        } else if(n->gate) {
            if(c->voice==BB_ACCENT)n->accent^=1u;else n->slide^=1u;
        }
        break;
    }
    case BB_CMD_NOTE_PITCH:core->synths[c->target].notes[c->step].note=(uint8_t)c->value;break;
    case BB_CMD_NOTE_SHIFT: {
        bb_note *n=&core->synths[c->target].notes[c->step];
        int pitch=(int)n->note+(int)c->value-127;
        n->note=(uint8_t)(pitch<0?0:pitch>127?127:pitch);break;
    }
    case BB_CMD_DRUM_MUTE:core->drums[c->target].muted=(uint8_t)c->value;break;
    case BB_CMD_DRUM_LEVEL:core->drums[c->target].level=(uint8_t)c->value;break;
    case BB_CMD_BAND_TOGGLE:core->band_muted^=(uint8_t)(1u<<c->target);break;
    case BB_CMD_BAND_LEVEL:core->band_level[c->target]=(uint8_t)c->value;break;
    case BB_CMD_DRUM_ENABLE:bb_set_enabled(core,c->target,c->value);break;
    case BB_CMD_DRUM_KIT:bb_recall_drum(core,c->target,c->value);break;
    case BB_CMD_DRUM_PARAMETER:bb_set_drum_parameter(core,c->target,c->voice,c->step,c->value);break;
#if BB_WITH_RICH_DRUM_RENDER
    case BB_CMD_DRUM_SOURCE_PARAMETER:bb_set_drum_source_parameter(core,c->target,c->voice,c->step,c->value);break;
#endif
    case BB_CMD_NOTE:core->synths[c->target].notes[c->step]=c->note;break;
    case BB_CMD_DILLO_PATCH:bb_recall_dillo(core,c->value);break;
    case BB_CMD_TELEGRAM_PATCH:bb_recall_telegram(core,&bb_telegram_bank[c->value].sound);break;
    case BB_CMD_SYNTH_MUTE:core->synths[c->target].muted=(uint8_t)c->value;break;
    case BB_CMD_SYNTH_LENGTH:bb_set_synth_length(core,c->target,c->value);break;
    case BB_CMD_SYNTH_PARAMETER:bb_synth_parameter_set(core,c->target,c->voice,c->value);break;
    case BB_CMD_FX_PARAMETER:bb_fx_parameter_set(core,c->voice,c->value);break;
    case BB_CMD_GROUP_LEVEL:case BB_CMD_GROUP_MUTE:
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_SAMPLE_KIT:case BB_CMD_SAMPLE_MIX:
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
    case BB_CMD_DRUM_PHRASE:
#if BB_SAMPLE_HOST_INTEGRATION
    case BB_CMD_KIT_PHRASE:
#endif
#endif
    case BB_CMD_909_PARAMETER:case BB_CMD_DRUM_TRIGGER:case BB_CMD_EVOLVE:case BB_CMD_PART_LAUNCH:case BB_CMD_SYNTH_PATTERN:case BB_CMD_DRUM_PATTERN:
    case BB_CMD_SYNTH_KEY:case BB_CMD_SYNTH_RELEASE:
    case BB_CMD_SYNTH_SOURCE_RELEASE:case BB_CMD_SYNTH_TRANSPOSE:case BB_CMD_TRIO_BANK:break; /* audio-owner events */
    }
}
unsigned bb_apply_pending_live(bb_core *core,bb_commands *q,bb_live_command_fn live,void *ctx) {
    unsigned n,i,stop;uintptr_t token;
    if(!core || !q)return 0;
    token=enter(q);n=q->count;stop=q->stop_pending;q->stop_pending=0;q->stop_inflight=(uint8_t)stop;leave(q,token);
    /* Drain only the entry snapshot: producers cannot make audio do unbounded work.
       One audio consumer; each short critical section copies just one command. */
    for(i=0;i<n;i++) {
        bb_command c;token=enter(q);c=q->items[q->read];
        q->read=(q->read+1u)%BB_COMMAND_CAPACITY;q->count--;leave(q,token);
        if(stop && c.type==BB_CMD_TRANSPORT && c.value)continue; /* superseded START */
        /* A redundant START is not a restart: keep the current beat and any
         * phrase queued for its next quantized boundary. */
        if(c.type==BB_CMD_TRANSPORT && c.value && core->running)continue;
#if BB_SAMPLE_HOST_INTEGRATION
        if(c.type!=BB_CMD_TRANSPORT && q->admit && !q->admit(q->admit_context,&c))continue;
#endif
        apply(core,&c);
        if((c.type==BB_CMD_GROUP_LEVEL || c.type==BB_CMD_GROUP_MUTE || c.type==BB_CMD_909_PARAMETER || c.type==BB_CMD_DRUM_TRIGGER || c.type==BB_CMD_EVOLVE || c.type==BB_CMD_PART_LAUNCH || c.type==BB_CMD_SYNTH_PATTERN || c.type==BB_CMD_DRUM_PATTERN
#if BB_SAMPLE_HOST_INTEGRATION
            || c.type==BB_CMD_SAMPLE_KIT || c.type==BB_CMD_SAMPLE_MIX
#endif
#if BB_WITH_RICH_DRUM_HANDOFF
            || c.type==BB_CMD_DRUM_PHRASE
#if BB_SAMPLE_HOST_INTEGRATION
            || c.type==BB_CMD_KIT_PHRASE
#endif
#endif
            || c.type==BB_CMD_TRANSPORT || c.type==BB_CMD_SYNTH_MUTE || c.type==BB_CMD_NOTE
            || c.type==BB_CMD_SYNTH_KEY || c.type==BB_CMD_SYNTH_RELEASE
            || c.type==BB_CMD_SYNTH_SOURCE_RELEASE || c.type==BB_CMD_SYNTH_TRANSPOSE || c.type==BB_CMD_TRIO_BANK
            ) && live)live(ctx,&c);
    }
    if(stop) {
        bb_command c={0};c.type=BB_CMD_TRANSPORT;
        apply(core,&c);if(live)live(ctx,&c);
        token=enter(q);q->stop_inflight=0;leave(q,token);
    }
    return n+stop;
}
unsigned bb_apply_pending_faulted(bb_core *core,bb_commands *q,bb_live_command_fn live,void *ctx) {
    unsigned n,i,stop;uintptr_t token;
    if(!core || !q)return 0;
    token=enter(q);n=q->count;stop=q->stop_pending;q->stop_pending=0;q->stop_inflight=(uint8_t)stop;leave(q,token);
    for(i=0;i<n;i++) {
        bb_command c;token=enter(q);c=q->items[q->read];
        q->read=(q->read+1u)%BB_COMMAND_CAPACITY;q->count--;leave(q,token);
        if(c.type==BB_CMD_TRANSPORT)continue; /* START refused; STOP applied below */
        if(c.type==BB_CMD_DRUM_KIT || c.type==BB_CMD_DILLO_PATCH || c.type==BB_CMD_TELEGRAM_PATCH)continue;
#if BB_SAMPLE_HOST_INTEGRATION
        if(q->admit && !q->admit(q->admit_context,&c))continue;
#endif
        apply(core,&c); /* no-op for audio-owner events */
        if(live)live(ctx,&c);
    }
    if(stop || core->running) {
        bb_command c={0};c.type=BB_CMD_TRANSPORT;
        apply(core,&c);if(live)live(ctx,&c);
    }
    token=enter(q);q->stop_inflight=0;leave(q,token);
    return n+stop;
}
unsigned bb_apply_pending(bb_core *core,bb_commands *q) {
    return bb_apply_pending_live(core,q,0,0);
}
unsigned bb_process_controlled(bb_core *core,bb_commands *q,uint32_t frames,
                               bb_hit_fn hit,bb_note_fn note,bb_render_fn render,void *ctx) {
    unsigned applied;
    if(!core || !frames)return 0;
    applied=bb_apply_pending(core,q);
    bb_process_render(core,frames,hit,note,render,ctx);return applied;
}
