/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio.h"
#include "bb_audio_family.h"
#include <string.h>
#if BB_WITH_EVOLVE
static int expression_valid(const bb_audio *a) {
#if BB_WITH_PATTERN
    for(unsigned s=0;s<BB_SYNTHS;s++)for(unsigned n=0;n<BB_STEPS;n++){
        bb_note_expression_view v;
        if(!bb_note_expression_read(a->patterns.expression[s][n],&v)||
           (v.has_velocity&&!v.velocity)||(v.has_gate&&!v.gate_percent))return 0;
    }
#else
    (void)a;
#endif
    return 1;
}
static int expression_matches(const bb_audio *a) {
#if BB_WITH_PATTERN
    return !memcmp(a->evolve_expression,a->patterns.expression,sizeof a->evolve_expression);
#else
    (void)a;return 1;
#endif
}
static void expression_capture(bb_audio *a) {
#if BB_WITH_PATTERN
    memcpy(a->evolve_expression,a->patterns.expression,sizeof a->evolve_expression);
#else
    (void)a;
#endif
}
int bb_audio_evolve_matches(const bb_audio *a) {
    return a&&bb_evolve_matches(&a->evolve,&a->core)&&expression_matches(a);
}
static void finish_home(bb_audio *a) {
    if(a->evolve.ready&&!bb_evolve_bind_families(&a->evolve,bb_audio_families(a))){a->evolve_result=BB_EV_RESULT_REJECTED;return;}
    a->evolve_home_pending=0;
    if(!bb_audio_evolve_matches(a)) {
        a->evolve.conflict=1;a->evolve.frozen=1;
        a->evolve_result=BB_EV_RESULT_CONFLICT;return;
    }
    a->evolve_result=bb_evolve_home(&a->evolve,&a->core)?BB_EV_RESULT_OK:BB_EV_RESULT_REJECTED;
}
void bb_audio_evolve_stopped(bb_audio *a) {
    if(!a->core.running && a->evolve_home_pending)finish_home(a);
}
void bb_audio_evolve_tick(void *ctx,bb_core *core) {
    bb_audio *a=ctx;
    if(a->evolve.ready&&!bb_evolve_bind_families(&a->evolve,bb_audio_families(a))){a->evolve_result=BB_EV_RESULT_REJECTED;return;}
    if(a->evolve_home_pending) {
        /* Global sixteen-step bar; unequal-length parts keep their clocks.
         * Detect authored edits immediately, not just at the return boundary. */
        if(!bb_audio_evolve_matches(a) || core->ticks%16u==0)finish_home(a);
    }
    if(a->evolve.ready&&a->evolve.enabled&&!a->evolve.frozen&&!expression_matches(a)){
        a->evolve.conflict=1;a->evolve.frozen=1;
        a->evolve_result=BB_EV_RESULT_CONFLICT;return;
    }
    bb_evolve_before_tick(&a->evolve,core);
    if(a->evolve.conflict)a->evolve_result=BB_EV_RESULT_CONFLICT;
}
void bb_audio_evolve_command(bb_audio *a,const bb_command *c) {
    bb_evolve *e=&a->evolve;
    if(!e->ready) {
        if(!expression_valid(a)||!bb_evolve_init_families(e,&a->core,0,bb_audio_families(a))){
            a->evolve_result=BB_EV_RESULT_REJECTED;return;
        }
        expression_capture(a);
    }
    if(!bb_evolve_bind_families(e,bb_audio_families(a))){a->evolve_result=BB_EV_RESULT_REJECTED;return;}
    if(a->evolve_home_pending) {
        if((c->voice==BB_EV_FREEZE && c->value==1) || (c->voice==BB_EV_ENABLE && !c->value)) {
            a->evolve_home_pending=0;e->frozen=1;
            if(c->voice==BB_EV_ENABLE)e->enabled=0;
            a->evolve_result=BB_EV_RESULT_CANCELLED;return;
        }
        a->evolve_result=BB_EV_RESULT_REJECTED;return;
    }
    a->evolve_result=BB_EV_RESULT_OK;
    switch(c->voice) {
    case BB_EV_RUN:e->enabled=1;e->frozen=0;return;
    case BB_EV_ENABLE:e->enabled=(uint8_t)c->value;return;
    case BB_EV_FREEZE:e->frozen=(uint8_t)c->value;return;
    case BB_EV_CAPTURE:
        /* Capturing changes no sounding notes; it freezes the snapshot taken
         * here, in FIFO order, at the audio block's command boundary. */
        if(!expression_valid(a)||!bb_evolve_capture(e,&a->core))a->evolve_result=BB_EV_RESULT_REJECTED;
        else expression_capture(a);
        return;
    case BB_EV_HOME:
        e->frozen=1;a->evolve_home_pending=1;
        a->evolve_result=BB_EV_RESULT_WAIT;
        if(!a->core.running)finish_home(a);
        return;
    default:break;
    }
    bb_evolve_settings cfg=e->settings;
    switch(c->voice) {
    case BB_EV_AMOUNT:cfg.amount=(uint8_t)c->value;break;
    case BB_EV_INTERVAL:cfg.every_phrases=(uint8_t)c->value;break;
    case BB_EV_PARTS:cfg.parts=(uint8_t)c->value;break;
    case BB_EV_ATTRIBUTES:cfg.attributes=(uint8_t)c->value;break;
    case BB_EV_ROOT:cfg.root=(uint8_t)c->value;break;
    case BB_EV_SCALE:cfg.scale=c->value;break;
    case BB_EV_LOW:cfg.low_note=(uint8_t)c->value;break;
    case BB_EV_HIGH:cfg.high_note=(uint8_t)c->value;break;
    case BB_EV_VOICES:cfg.drum_voices=c->value;break;
    case BB_EV_LOCK:
        if(c->value)cfg.locked[c->target]|=UINT64_C(1)<<c->step;
        else cfg.locked[c->target]&=~(UINT64_C(1)<<c->step);
        break;
    default:a->evolve_result=BB_EV_RESULT_REJECTED;return;
    }
    if(!bb_evolve_configure(e,&cfg))a->evolve_result=BB_EV_RESULT_REJECTED;
}
#endif
