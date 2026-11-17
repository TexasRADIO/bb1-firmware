/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_audio_session.h"
#include "bb_one_drum_profile.h"

bb_audio_session_result bb_audio_session_restore(bb_audio *a,
    const uint8_t *wire,size_t length,bb_audio_session_workspace *w) {
    bb_enter_fn enter;bb_leave_fn leave;void *context;unsigned k;
    if(!a || !wire || !w || a->core.sample_rate!=44100 ||
       (!a->commands.enter != !a->commands.leave))return BB_AUDIO_SESSION_INVALID;
    if(!a->groups || !bb_audio_groups_outside(a,wire,length) ||
       !bb_audio_groups_outside(a,w,sizeof *w))return BB_AUDIO_SESSION_INVALID;
    gg_state *saved_groups=a->groups;
    if(a->core.running || !bb_commands_idle(&a->commands))return BB_AUDIO_SESSION_BUSY;
#if BB_WITH_PARTS
    if(a->trio.state!=BB_TRIO_FREE)return BB_AUDIO_SESSION_BUSY;
#if BB_WITH_PREPARED_BANK
    if(!pb_ownership_idle(&a->parts))return BB_AUDIO_SESSION_BUSY;
#endif
#if BB_TRIO_EXTERNAL_PROGRAM
    bb_trio_program *program=a->trio.program;
#endif
#endif
#if BB_WITH_PATTERN
    for(k=0;k<BB_PATTERN_SLOTS;k++)
        if(a->patterns.inbox[k].state!=BB_PATTERN_SLOT_FREE ||
           a->drum_patterns.inbox[k].state!=BB_PATTERN_SLOT_FREE)return BB_AUDIO_SESSION_BUSY;
#endif
    if(!bb_session_decode(wire,length,44100,&w->decoded,&w->scratch))return BB_AUDIO_SESSION_INVALID;
    if(!bb_one_drum_core_ok(&w->decoded))return BB_AUDIO_SESSION_INVALID;
    enter=a->commands.enter;leave=a->commands.leave;context=a->commands.lock_context;
    if(!bb_audio_init(a,enter,leave,context))return BB_AUDIO_SESSION_INIT_FAILED;
#if BB_WITH_PARTS && BB_TRIO_EXTERNAL_PROGRAM
    a->trio.program=program;
#endif
    /* Existing scene format has no group faders: retain the current live
     * group mix across this exclusive restore, without claiming persistence. */
    a->groups=saved_groups;
    a->core=w->decoded;
    for(k=0;k<BB_MACHINES;k++) {
        /* Decoder revision starts at 1, just like a fresh default engine.
         * Force the restored sound through the real adapter on first render,
         * even when a saved kit's numeric ID matches the default kit. */
        a->drum_revision[k]=0;
        a->mix_gain[k]=((uint32_t)a->core.drums[k].level*65536u+63u)/127u;
    }
    for(k=0;k<BB_MIX_CHANNELS;k++)
        a->band_gain[k]=((uint32_t)a->core.band_level[k]*65536u+63u)/127u;
    return BB_AUDIO_SESSION_OK;
}
