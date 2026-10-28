/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_sound_picker.h"
#include <string.h>
#if defined(BB_SAMPLE_HYBRID_TRIAL)
#include "bb_hybrid_selection.h"
static int hybrid_index(unsigned id,unsigned count){
    bb_hybrid_selection s={id==BB_SAMPLE_808_ID?BB_HYBRID_808:BB_HYBRID_SAMPLE,
                           id==BB_SAMPLE_808_ID?0:(uint8_t)id};
    unsigned index;
    return bb_hybrid_to_index(s,count,&index)?(int)index:-1;
}
#endif

unsigned bb_sound_picker_count(bb_page page) {
    if(page==BB_DRUMS)return BB_DRUM_KITS
#if BB_WITH_909_ROUTE
        +1u
#endif
        ;
#if BB_WITH_DILLO
    if(page==BB_DILLO)return BB_DILLO_PATCHES;
#endif
#if BB_WITH_TELEGRAM
    if(page==BB_TELEGRAM)return BB_TELEGRAM_PATCHES;
#endif
    return 0;
}
const char *bb_sound_picker_name(bb_page page,unsigned i) {
    if(i>=bb_sound_picker_count(page))return NULL;
    return page==BB_DRUMS?(i==BB_DRUM_KITS?"Oh Nine Oh":bb_drum_kit_names[i]):
           page==BB_DILLO?bb_dillo_bank[i].name:bb_telegram_bank[i].name;
}
unsigned bb_sound_picker_items(const bb_sound_picker *p) {
    if(!p)return 0;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count)return p->sample_count
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        +1u
#endif
        ;
#endif
    return bb_sound_picker_count((bb_page)p->page);
}
const char *bb_sound_picker_item_name(const bb_sound_picker *p,unsigned i){
    if(!p||i>=bb_sound_picker_items(p))return NULL;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count){
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        return i?p->sample_names[i-1]:"808";
#else
        return p->sample_names[i];
#endif
    }
#endif
    return bb_sound_picker_name((bb_page)p->page,i);
}
static int valid(const bb_sound_picker *p) {
    return p&&p->ready&&bb_sound_picker_items(p)&&
           p->machine<BB_MACHINES&&p->cursor<bb_sound_picker_items(p);
}
int bb_sound_picker_available(const bb_sound_picker *p,const bb_sound_picker_view *v,unsigned i){
    if(!valid(p)||!v||i>=bb_sound_picker_items(p))return 0;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count)return !!(v->sample_compatible&(1u<<i));
#endif
    return 1;
}
/* Compare fields, not struct padding. A custom/scene sound is never labelled
 * as a factory preset merely because an old UI index still points to one. */
static int dillo_equal(const bb_dillo_patch *a,const bb_dillo_patch *b) {
    return a->cutoff_hz==b->cutoff_hz&&a->resonance_x100==b->resonance_x100&&
        a->wave==b->wave&&a->envelope==b->envelope&&a->decay==b->decay&&
        a->accent==b->accent&&a->drive==b->drive&&a->slide_ms==b->slide_ms&&
        a->attack_ms==b->attack_ms&&a->tracking==b->tracking;
}
static int telegram_equal(const bb_telegram_patch *a,const bb_telegram_patch *b) {
    return a->cutoff_hz==b->cutoff_hz&&a->resonance_x100==b->resonance_x100&&
        a->square==b->square&&a->saw==b->saw&&a->sub==b->sub&&
        a->envelope==b->envelope&&a->modulation==b->modulation&&a->glide==b->glide&&
        a->lfo_rate_x100==b->lfo_rate_x100&&a->attack_ms==b->attack_ms&&
        a->release_ms==b->release_ms&&a->glide_ms==b->glide_ms;
}
int bb_sound_picker_observe(const bb_sound_picker *p,bb_audio *a,bb_sound_picker_view *out) {
    if(!valid(p)||!a||!out||(!a->commands.enter!=!a->commands.leave))return 0;
    bb_sound_picker_view view;memset(&view,0,sizeof view);view.active_index=-1;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count){
        bb_sample_status s;
        if(p->page!=BB_DRUMS||!bb_audio_sample_status_layer(a,p->machine,&s)||s.count!=p->sample_count)return 0;
        view.active_index=s.selected<s.count?s.selected:-1;
        view.family_epoch=s.serial;view.queue_idle=s.queue_idle;view.stop_queued=s.stop_pending;
        view.sample_compatible=s.compatible;view.sample_result=s.result;view.sample_requested=s.requested;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        view.active_index=hybrid_index(s.selected,s.count);
        view.sample_requested=(uint8_t)hybrid_index(s.requested,s.count);
        view.sample_compatible=(uint16_t)((unsigned)s.compatible<<1);
        if(s.native_compatible)view.sample_compatible|=1u;
#endif
        uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
        view.running=a->core.running;view.enabled=a->core.drums[p->machine].enabled;view.muted=a->core.drums[p->machine].muted;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        if(s.selected==BB_SAMPLE_808_ID)view.edited=a->core.drums[p->machine].sound.edited;
#endif
        if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
        *out=view;return 1;
    }
    if(p->page==BB_DRUMS&&bb_audio_sample_layer(a,p->machine))return 0; /* bound samples are not an 808 recipe */
#if !defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    if(p->page==BB_DRUMS&&a->sample&&p->machine)return 0;
#else
    if(p->page==BB_DRUMS&&a->sample&&p->machine>1)return 0;
#endif
#endif
    union {bb_dillo_patch dillo;bb_telegram_patch telegram;} sound;
    unsigned recipe=0,family=0;
    uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
    view.queue_idle=(uint8_t)bb_commands_idle(&a->commands);
    view.stop_queued=(uint8_t)(a->commands.stop_pending||a->commands.stop_inflight);
    view.running=a->core.running;
    if(p->page==BB_DRUMS) {
        const bb_machine *m=&a->core.drums[p->machine];
        recipe=m->kit;family=a->drum_patterns.family[p->machine];
        view.family_epoch=a->drum_patterns.family_epoch[p->machine];
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT) && !BB_WITH_909_ROUTE
        /* This profile recalls 808 recipes through the command consumer,
         * not a worker family transaction. Observe its actual revision. */
        view.family_epoch=m->sound_revision;
#endif
        view.edited=m->sound.edited;view.enabled=m->enabled;view.muted=m->muted;
    } else {
        unsigned s=p->page==BB_TELEGRAM;
        view.enabled=1;view.muted=a->core.synths[s].muted;
        if(s)sound.telegram=a->core.telegram;else sound.dillo=a->core.dillo;
    }
    if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
    if(p->page==BB_DRUMS) {
        if(family>1u||(family==0u&&recipe>=BB_DRUM_KITS)||
           (family==1u&&bb_sound_picker_count(BB_DRUMS)<=BB_DRUM_KITS))return 0;
        view.active_index=(int)(family?BB_DRUM_KITS:recipe);
        if(family)view.edited=0; /* 909 is an engine identity, not a preset claim. */
    } else {
        for(unsigned i=0;i<bb_sound_picker_count((bb_page)p->page);i++) {
            int same=p->page==BB_DILLO?dillo_equal(&sound.dillo,&bb_dillo_bank[i].sound):
                telegram_equal(&sound.telegram,&bb_telegram_bank[i].sound);
            if(same){view.active_index=(int)i;break;}
        }
        view.edited=(uint8_t)(view.active_index<0);
    }
    *out=view;return 1;
}
int bb_sound_picker_init(bb_sound_picker *p,bb_audio *a,bb_page page,unsigned machine) {
    if(!p||!a||!bb_sound_picker_count(page)||machine>=BB_MACHINES)return 0;
    bb_sound_picker next;memset(&next,0,sizeof next);
    next.page=(uint8_t)page;next.machine=(uint8_t)machine;next.ready=1;
    bb_sound_picker_view view;
    if(!bb_sound_picker_observe(&next,a,&view))return 0;
    if(view.active_index>=0)next.cursor=(uint8_t)view.active_index;
    *p=next;return 1;
}
#if BB_SAMPLE_HOST_INTEGRATION
static int sample_init_layer(bb_sound_picker *p,bb_audio *a,unsigned machine,const char *const *names,unsigned count){
    if(!p||!a||!names||!count||count>8)return 0;
    for(unsigned i=0;i<count;i++)if(!names[i]||!names[i][0])return 0;
    bb_sound_picker next={0};next.page=BB_DRUMS;next.ready=1;
    if(machine>=2)return 0;
    next.machine=(uint8_t)machine;
    next.sample_count=(uint8_t)count;next.sample_names=names;
    bb_sound_picker_view v;if(!bb_sound_picker_observe(&next,a,&v))return 0;
    if(v.active_index>=0)next.cursor=(uint8_t)v.active_index;
    *p=next;return 1;
}
int bb_sound_picker_sample_init(bb_sound_picker *p,bb_audio *a,const char *const *names,unsigned count){
    return sample_init_layer(p,a,0,names,count);
}
#endif
int bb_sound_picker_open(bb_sound_picker *p,bb_audio *a,bb_page page,unsigned machine,
                         const char *const *names,unsigned count) {
#if BB_SAMPLE_HOST_INTEGRATION
    if(a&&page==BB_DRUMS&&bb_audio_sample_layer(a,machine))
        return sample_init_layer(p,a,machine,names,count);
    if(a&&page==BB_DRUMS&&a->sample) {
        /* The independent B engine currently has synthesized recipes only.
         * Do not attach A's sample catalogue or sample acknowledgement to B. */
        if(machine) {
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
            if(machine==1)return bb_sound_picker_init(p,a,page,machine);
#endif
            return 0;
        }
        return bb_sound_picker_sample_init(p,a,names,count);
    }
#else
    (void)names;(void)count;
#endif
    return bb_sound_picker_init(p,a,page,machine);
}
int bb_sound_picker_move(bb_sound_picker *p,int delta) {
    if(!valid(p))return 0;
    int count=(int)bb_sound_picker_items(p);
    if(delta>count)delta=count;if(delta< -count)delta= -count;
    int next=(int)p->cursor+delta;
    p->cursor=(uint8_t)(next<0?0:next>=count?count-1:next);return 1;
}
int bb_sound_picker_key(bb_sound_picker *p,unsigned key) {
    if(!valid(p)||key>=bb_sound_picker_items(p))return 0;
    p->cursor=(uint8_t)key;return 1;
}
int bb_sound_picker_commit(bb_sound_picker *p,bb_audio *a,const bb_sound_picker_kits *kits) {
    bb_sound_picker_view view;
    if(!bb_sound_picker_observe(p,a,&view))return BB_SP_INVALID;
    if(p->pending||view.stop_queued)return BB_SP_BUSY;
    if(view.queue_idle&&view.active_index==p->cursor&&!view.edited) {
        p->status=BB_SP_SAME;return BB_SP_UNCHANGED;
    }
    int accepted;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count){
        if(!view.queue_idle)return BB_SP_BUSY;
        if(!(view.sample_compatible&(1u<<p->cursor))){p->status=BB_SP_FAILED;return BB_SP_REFUSED;}
        bb_command command={0};command.type=BB_CMD_SAMPLE_KIT;command.value=p->cursor;
        command.target=p->machine;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
        bb_hybrid_selection selection;
        if(!bb_hybrid_from_index(p->cursor,p->sample_count,&selection))return BB_SP_INVALID;
        command.value=selection.kind==BB_HYBRID_808?BB_SAMPLE_808_ID:selection.sample;
#endif
        accepted=bb_command_push(&a->commands,&command);
    } else
#endif
    if(p->page==BB_DRUMS) {
        if(!kits||!kits->request||!kits->busy)return BB_SP_INVALID;
        if(kits->busy(kits->context))return BB_SP_BUSY;
        accepted=kits->request(kits->context,p->machine,p->cursor==BB_DRUM_KITS,
                               p->cursor==BB_DRUM_KITS?0u:p->cursor);
    } else {
        bb_command command={0};command.target=(uint8_t)(p->page==BB_TELEGRAM);
        command.type=p->page==BB_DILLO?BB_CMD_DILLO_PATCH:BB_CMD_TELEGRAM_PATCH;
        command.value=p->cursor;accepted=bb_command_push(&a->commands,&command);
    }
    if(!accepted){p->status=BB_SP_FAILED;return BB_SP_REFUSED;}
    p->requested=p->cursor;p->epoch_before=view.family_epoch;
    p->pending=1;p->status=BB_SP_REQUESTED;return BB_SP_OK;
}
int bb_sound_picker_poll_view(bb_sound_picker *p,bb_audio *a,const bb_sound_picker_kits *kits,bb_sound_picker_view *out) {
    bb_sound_picker_view view;
    if(!bb_sound_picker_observe(p,a,&view))return 0;
    if(p->pending&&p->page==BB_DRUMS
#if BB_SAMPLE_HOST_INTEGRATION
       &&!p->sample_count
#endif
       &&(!kits||!kits->busy))return 0;
    if(out)*out=view;
    if(!p->pending)return 1;
#if BB_SAMPLE_HOST_INTEGRATION
    if(p->sample_count){
        if(view.family_epoch==p->epoch_before){
            /* A kit select completes in the audio owner's command consumer.
             * If the queue drained without its serial changing, admission
             * dropped the request; waiting for an acknowledgement now would
             * leave the picker blocked forever. */
            if(view.queue_idle){p->status=BB_SP_FAILED;p->pending=0;}
            return 1;
        }
        p->status=(uint32_t)(view.family_epoch-p->epoch_before)!=1u||view.sample_requested!=p->requested?
            BB_SP_SUPERSEDED:view.sample_result==1&&view.active_index==p->requested?BB_SP_APPLIED:BB_SP_FAILED;
        p->pending=0;return 1;
    }
#endif
    if(p->page==BB_DRUMS) {
        if(!kits||!kits->busy)return 0;
        if(kits->busy(kits->context))return 1;
        /* D5 (reliability r1): recalling the active, unedited recipe is a
         * no-op that never advances the revision. With the queue busy at
         * commit (so UNCHANGED was not returned) the epoch test below would
         * report FAILED although the requested kit is the one loaded. */
        if(view.family_epoch==p->epoch_before && view.queue_idle &&
           view.active_index==p->requested && !view.edited){
            p->status=BB_SP_APPLIED;p->pending=0;return 1;
        }
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT) && !BB_WITH_909_ROUTE
        if(!view.queue_idle)return 1; /* queued is not applied */
#endif
        p->status=view.family_epoch!=p->epoch_before&&view.active_index==p->requested?
            BB_SP_APPLIED:BB_SP_FAILED;
    } else {
        if(!view.queue_idle)return 1;
        /* Current applied values, not a new audio acknowledgement token.
         * A later command can supersede a preset before this poll. */
        p->status=view.active_index==p->requested?BB_SP_APPLIED:BB_SP_SUPERSEDED;
    }
    p->pending=0;return 1;
}
int bb_sound_picker_poll(bb_sound_picker *p,bb_audio *a,const bb_sound_picker_kits *kits) {
    return bb_sound_picker_poll_view(p,a,kits,NULL);
}
