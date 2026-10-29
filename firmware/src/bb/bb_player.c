#include "bb_player.h"
#include "bb_factory_recipe.h"
#include <string.h>
#include "bb_voice_fallback.h"
static int init(bb_player *p,const bb_browser *seed,unsigned target,int drum) {
    bb_factory_choice choice;
    if(!p || !seed || target>=(drum?BB_MACHINES:BB_SYNTHS) || !bb_browser_current(seed,&choice))return 0;
    bb_player next;memset(&next,0,sizeof next);next.view=*seed;
    next.target=(uint8_t)target;next.is_drum=(uint8_t)drum;next.ready=1;*p=next;return 1;
}
int bb_player_init(bb_player *p,const bb_browser *seed,unsigned synth){return init(p,seed,synth,0);}
int bb_player_init_drum(bb_player *p,const bb_browser *seed,unsigned machine){return init(p,seed,machine,1);}
/* Single-producer copied requests have monotonically allocated nonzero
 * tickets. Real queued/applied payloads prove earlier FIFO consumption, even
 * if the main loop missed an intermediate snapshot. Raw rejection tickets
 * do not provide that ordering evidence. Reset after audio reinitialization;
 * one controller generation must not span half the uint32 ticket space. */
static int reached(uint32_t ticket,uint32_t observed) {
    return observed && (uint32_t)(observed-ticket)<UINT32_C(0x80000000);
}
static int seen(const bb_pattern_status *s,uint32_t ticket) {
    return ticket && (s->seen==ticket || reached(ticket,s->queued) || reached(ticket,s->applied));
}
static int sync_with(bb_player *p,bb_audio *a,const bb_player_selection *incoming,unsigned rich_request) {
#if BB_WITH_PATTERN
    bb_pattern_status status;
    if(!p || !p->ready)return 0;
    if(p->is_drum) {
        bb_drum_pattern_status report;
#if BB_WITH_RICH_DRUM_BROWSER
        if(!bb_audio_drum_selection_status(a,p->target,rich_request,&report))return 0;
#else
        (void)rich_request;
        if(!bb_audio_drum_pattern_status(a,p->target,&report))return 0;
#endif
        status=report.state;
        uint32_t expected=incoming?incoming->ticket:p->request.ticket;
        if(expected && status.seen==expected && status.result==BB_PATTERN_REJECTED)
            p->error=report.error==BB_DRUM_PATTERN_ERROR_MODE?BB_PLAYER_NEEDS_KIT:
                     report.error==BB_DRUM_PATTERN_ERROR_FAULT?BB_PLAYER_AUDIO_FAULT:BB_FACTORY_PATTERN_INVALID;
        /* D4: a queued request ended by STOP is terminal, not silently idle. */
        else if(expected && status.seen==expected && status.result==BB_PATTERN_CANCELLED)
            p->error=BB_PLAYER_CANCELLED;
    } else {
        if(!bb_audio_pattern_status(a,p->target,&status))return 0;
        /* D4 for Dillo/Telegram: the same terminal CANCELLED outcome as a drum layer, so the one
         * core announcer (bb_library_sync) says it; no second owner in the UI. */
        uint32_t expected=incoming?incoming->ticket:p->request.ticket;
        if(expected && status.seen==expected && status.result==BB_PATTERN_CANCELLED)p->error=BB_PLAYER_CANCELLED;
    }
    if(!status.applied)memset(&p->playing,0,sizeof p->playing);
    else if(incoming && incoming->ticket==status.applied)p->playing=*incoming;
    else if(p->request.ticket==status.applied)p->playing=p->request;
    else if(p->playing.ticket!=status.applied)memset(&p->playing,0,sizeof p->playing);
    if(seen(&status,p->request.ticket))p->request_seen=1;
    p->status=status;return 1;
#else
    (void)p;(void)a;(void)incoming;(void)rich_request;return 0;
#endif
}
int bb_player_sync(bb_player *p,bb_audio *a) {return sync_with(p,a,NULL,
#if BB_WITH_RICH_DRUM_BROWSER
    p?p->request_rich:0
#else
    0
#endif
);}
static int launch(bb_player *p,bb_audio *a,const bb_browser *view,int kit) {
    bb_factory_choice choice;
    if(!p || !p->ready || !a || !bb_browser_current(view,&choice))return BB_FACTORY_PATTERN_INVALID;
    (void)bb_player_sync(p,a);
    bb_player_selection incoming;memset(&incoming,0,sizeof incoming);incoming.choice=choice;
    if(kit>=0){incoming.kit=(uint8_t)kit;incoming.has_kit=1;}
    int result;unsigned rich_request=0;
    if(p->is_drum) {
        int drum;
#if BB_WITH_RICH_DRUM_BROWSER
        rich_request=p->target<BB_RICH_DRUMS&&!a->drum_patterns.family[p->target];
        if(rich_request && kit>=0) {
#if BB_SAMPLE_HOST_INTEGRATION
            bb_phrase_index phrase;
            drum=bb_factory_prepare_rich_drum(view->catalogue,&choice,&phrase);
            if(drum==BB_FDRUM_OK) {
                uint32_t voices=(unsigned)kit==BB_SAMPLE_808_ID?(BB_RICH_808_VOICES|bb_audio_aux_capabilities(a)):
                    bb_audio_sample_source_voices(a,(unsigned)kit);
                if(((unsigned)kit!=BB_SAMPLE_808_ID && (unsigned)kit>=a->sample_catalogue_count) ||
                   !bb_phrase_index_valid(&phrase,voices))drum=BB_FDRUM_VOICE;
                else if(!bb_audio_phrase_preflight_playable(&phrase,voices))drum=BB_FDRUM_EXPRESSION;
                else if(!bb_audio_kit_phrase_submit(a,p->target,(unsigned)kit,&phrase,16,&incoming.ticket))
                    drum=BB_FDRUM_BUSY;
            }
#else
            drum=BB_FDRUM_UNAVAILABLE;
#endif
        } else if(kit>=0)drum=BB_FDRUM_UNAVAILABLE;
        else if(rich_request)drum=bb_factory_request_rich_drum(a,view->catalogue,&choice,p->target,16,&incoming.ticket);
        else
#endif
        drum=bb_factory_request_drum_pattern(a,view->catalogue,&choice,p->target,16,&incoming.ticket);
        result=drum==BB_FDRUM_OK?BB_FACTORY_PATTERN_OK:
               drum==BB_FDRUM_BUSY?BB_FACTORY_PATTERN_BUSY:
               drum==BB_FDRUM_UNAVAILABLE?BB_FACTORY_PATTERN_UNAVAILABLE:
               drum==BB_FDRUM_TIMING?BB_PLAYER_NEEDS_TIMING:
               drum==BB_FDRUM_VOICE?BB_PLAYER_NEEDS_VOICE:
               drum==BB_FDRUM_EXPRESSION?BB_FACTORY_PATTERN_EXPRESSION:BB_FACTORY_PATTERN_INVALID;
    } else result=bb_factory_request_synth_pattern(a,view->catalogue,&choice,p->target,16,&incoming.ticket);
    p->view=*view;p->error=(uint8_t)result;
    if(result==BB_FACTORY_PATTERN_OK && p->is_drum && choice.group==BB_BROWSER_SONGS &&
       !(p->request.choice.group==BB_BROWSER_SONGS && p->request.choice.bank_index==choice.bank_index)) {
        /* 1.5: a new song brings its sheet tempo and swing, as an era does; another section of the same song keeps
         * the player's tempo. Best effort: a full command queue leaves the tempo as it is, never the drums. */
        unsigned bpm,swing_x2;
        if(bb_factory_read_song_tempo(view->catalogue,&choice,&bpm,&swing_x2)) {
            bb_command t={0};t.type=BB_CMD_TEMPO;t.value=(uint16_t)bpm;(void)bb_command_push(&a->commands,&t);
            bb_command s={0};s.type=BB_CMD_SWING_X2;s.value=(uint16_t)swing_x2;(void)bb_command_push(&a->commands,&s);
        }
    }
    if(result==BB_FACTORY_PATTERN_OK) {
        /* Audio may consume the prior or new request before this producer
         * returns. Resolve both identities before replacing the old mapping. */
        (void)sync_with(p,a,&incoming,rich_request);p->request=incoming;
#if BB_WITH_RICH_DRUM_BROWSER
        p->request_rich=(uint8_t)rich_request;
#endif
        p->request_seen=(uint8_t)seen(&p->status,incoming.ticket);
    }
    return result;
}
int bb_player_bank_key(bb_player *p,bb_audio *a,unsigned key) {
    if(!p || !p->ready)return BB_FACTORY_PATTERN_INVALID;
    bb_browser view=p->view;
    if(!bb_browser_bank_key(&view,key,NULL))return BB_FACTORY_PATTERN_INVALID;
    return launch(p,a,&view,-1);
}
int bb_player_part_key(bb_player *p,bb_audio *a,unsigned key) {
    if(!p || !p->ready)return BB_FACTORY_PATTERN_INVALID;
    bb_browser view=p->view;
    if(!bb_browser_part_key(&view,key,NULL))return BB_FACTORY_PATTERN_INVALID;
    return launch(p,a,&view,-1);
}
int bb_player_move(bb_player *p,bb_audio *a,int delta) {
    if(!p || !p->ready)return BB_FACTORY_PATTERN_INVALID;
    bb_browser view=p->view;
    if(!bb_browser_move_bank(&view,delta))return BB_FACTORY_PATTERN_INVALID;
    return launch(p,a,&view,-1);
}
int bb_player_group(bb_player *p,unsigned group) {
    if(!p || !p->ready)return 0;
    if(p->is_drum && group==BB_BROWSER_ACID)return 0;   /* ACID holds Dillo phrases only */
    return bb_browser_group(&p->view,group);
}
int bb_player_page(bb_player *p,int delta,int sections) {
    if(!p || !p->ready || (sections!=0 && sections!=1))return 0;
    return sections?bb_browser_move_part_page(&p->view,delta):bb_browser_move_bank_page(&p->view,delta);
}
unsigned bb_player_pending(const bb_player *p) {
    if(!p || !p->ready || !p->request.ticket || p->status.applied==p->request.ticket)return 0;
    if(p->status.queued==p->request.ticket)return 2;
    return p->request_seen?0u:1u;
}

/* SPDX-License-Identifier: GPL-3.0-only
 * Additive experiment appended to a verified copy of bb_player.c.
 * Production and old entry points are unchanged.
 */
#include "bb_player_preview.h"
int bb_player_preview_move(bb_player *p,int delta) {
    bb_factory_choice current;
    if(!p || !p->ready || !bb_browser_current(&p->view,&current))return 0;
    if(!delta)return 1;
    bb_browser next=p->view;
    if(!bb_browser_move_bank(&next,delta))return 0;
    p->view=next;return 1;
}
int bb_player_preview_bank_key(bb_player *p,unsigned key) {
    if(!p || !p->ready)return 0;
    bb_browser next=p->view;
    if(!bb_browser_bank_key(&next,key,NULL))return 0;
    p->view=next;return 1;
}
int bb_player_preview_part_key(bb_player *p,unsigned key) {
    if(!p || !p->ready)return 0;
    bb_browser next=p->view;
    if(!bb_browser_part_key(&next,key,NULL))return 0;
    p->view=next;return 1;
}
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
static uint32_t preview_kit_voices(const bb_audio *a,unsigned machine,unsigned kit) {
    if(!a || !bb_audio_sample_layer(a,machine))return 0;
    if(kit==BB_SAMPLE_808_ID)return BB_RICH_808_VOICES|bb_audio_aux_capabilities(a);
    return kit<a->sample_catalogue_count?bb_audio_sample_source_voices(a,kit):0;
}
static int preview_choice_playable(const bb_browser *view,const bb_audio *a,unsigned machine,unsigned kit) {
    bb_factory_choice choice;bb_phrase_index phrase;
    uint32_t voices=preview_kit_voices(a,machine,kit);
    return voices && bb_browser_current(view,&choice) &&
        bb_factory_prepare_rich_drum(view->catalogue,&choice,&phrase)==BB_FDRUM_OK &&
        bb_audio_phrase_preflight_playable(&phrase,voices);
}
int bb_player_preview_kit_playable(const bb_player *p,const bb_audio *a,unsigned kit) {
    return p&&p->ready&&p->is_drum&&p->target<BB_RICH_DRUMS&&a&&
        !a->drum_patterns.family[p->target]&&preview_choice_playable(&p->view,a,p->target,kit);
}
int bb_player_preview_substitutions(const bb_player *p,const bb_audio *a,unsigned kit,
                                    unsigned *count,unsigned *first,unsigned *replacement) {
    if(!count||!first||!replacement||!p||!p->ready||!p->is_drum||!a||
       p->target>=BB_RICH_DRUMS||!bb_audio_sample_layer(a,p->target)||
       (kit!=BB_SAMPLE_808_ID&&kit>=a->sample_catalogue_count))return 0;
    bb_factory_choice choice;bb_phrase_index phrase;
    if(!bb_browser_current(&p->view,&choice)||
       bb_factory_prepare_rich_drum(p->view.catalogue,&choice,&phrase)!=BB_FDRUM_OK)return 0;
    uint32_t native=kit==BB_SAMPLE_808_ID?BB_RICH_808_NATIVE_VOICES:
                     a->sample_catalogue[kit].source_voices;
    /* Drum B's native-808 release path substitutes auxiliary-only sources so
     * the worker and serial paths stay identical.  Excluding AUX here makes
     * the existing selected-pair footer disclose SY>CB / ZAP>HT. */
    if(!(p->target==1&&kit==BB_SAMPLE_808_ID))native|=bb_audio_aux_capabilities(a);
    unsigned n=0,src=BB_FALLBACK_NONE,dst=BB_FALLBACK_NONE;
    for(unsigned v=0;v<BB_PHRASE_VOICES;v++)if(phrase.voices&(1u<<v)){
        unsigned routed=bb_voice_fallback(v,native);
        if(routed==BB_FALLBACK_NONE)return 0;
        if(routed!=v){if(!n){src=v;dst=routed;}n++;}
    }
    *count=n;*first=src;*replacement=dst;return 1;
}
/* Try the current section first, then its siblings; only move to a different
 * bank when no section in this one belongs to the chosen kit. This keeps
 * authored initial sections meaningful whenever they are playable. */
static int preview_align_bank(bb_browser *view,const bb_audio *a,unsigned machine,unsigned kit) {
    size_t count=bb_browser_part_count(view);
    unsigned first=view->part_index;
    for(size_t n=0;n<count;n++){
        unsigned index=(unsigned)((first+n)%count);
        bb_browser candidate=*view;
        candidate.part_index=(uint16_t)index;
        candidate.part_page=(uint16_t)(index/BB_BROWSER_PART_KEYS);
        if(preview_choice_playable(&candidate,a,machine,kit)){*view=candidate;return 1;}
    }
    return 0;
}
static int preview_align(bb_browser *view,const bb_audio *a,unsigned machine,unsigned kit) {
    size_t banks=bb_browser_bank_count(view);
    for(size_t b=0;b<banks;b++){
        if(preview_align_bank(view,a,machine,kit))return 1;
        if(!bb_browser_move_bank(view,1))return 0;
    }
    return 0;
}
static int preview_commit_kit(bb_player *p,const bb_audio *a,unsigned kit,bb_browser *next) {
    if(!p||!p->ready||!p->is_drum||p->target>=BB_RICH_DRUMS||!a||
       a->drum_patterns.family[p->target]||!preview_kit_voices(a,p->target,kit)||
       !preview_align(next,a,p->target,kit))return 0;
    p->view=*next;return 1;
}
int bb_player_preview_align_kit(bb_player *p,const bb_audio *a,unsigned kit) {
    if(!p||!p->ready)return 0;
    bb_browser next=p->view;
    return preview_commit_kit(p,a,kit,&next);
}
int bb_player_preview_move_kit(bb_player *p,const bb_audio *a,unsigned kit,int delta) {
    if(!p||!p->ready||!p->is_drum||p->target>=BB_RICH_DRUMS||!a||
       a->drum_patterns.family[p->target])return 0;
    if(!delta)return bb_player_preview_kit_playable(p,a,kit);
    bb_browser next=p->view;
    if(!bb_browser_move_bank(&next,delta))return 0;
    int direction=delta<0?-1:1;
    size_t banks=bb_browser_bank_count(&next);
    for(size_t b=0;b<banks;b++){
        bb_browser candidate=next;
        if(preview_kit_voices(a,p->target,kit)&&
           preview_align_bank(&candidate,a,p->target,kit)){
            p->view=candidate;return 1;
        }
        if(!bb_browser_move_bank(&next,direction))return 0;
    }
    return 0;
}
int bb_player_preview_bank_key_kit(bb_player *p,const bb_audio *a,unsigned kit,unsigned key) {
    if(!p||!p->ready||!p->is_drum||p->target>=BB_RICH_DRUMS||!a||
       a->drum_patterns.family[p->target])return 0;
    bb_browser next=p->view;
    if(!bb_browser_bank_key(&next,key,NULL)||!preview_kit_voices(a,p->target,kit)||
       !preview_align_bank(&next,a,p->target,kit))return 0;
    p->view=next;return 1;
}
int bb_player_preview_part_key_kit(bb_player *p,const bb_audio *a,unsigned kit,unsigned key) {
    if(!p||!p->ready||!p->is_drum||p->target>=BB_RICH_DRUMS||!a||
       a->drum_patterns.family[p->target])return 0;
    bb_browser next=p->view;
    if(!bb_browser_part_key(&next,key,NULL)||!preview_choice_playable(&next,a,p->target,kit))return 0;
    p->view=next;return 1;
}
int bb_player_preview_group_kit(bb_player *p,const bb_audio *a,unsigned kit,unsigned group) {
    if(!p||!p->ready)return 0;
    bb_browser next=p->view;
    return bb_browser_group(&next,group)&&preview_commit_kit(p,a,kit,&next);
}
#endif
int bb_player_confirm(bb_player *p,bb_audio *a) {
    if(!p || !p->ready || !a)return BB_FACTORY_PATTERN_INVALID;
    const bb_browser selected=p->view;
    return launch(p,a,&selected,-1);
}
int bb_player_confirm_with_kit(bb_player *p,bb_audio *a,unsigned kit) {
    if(!p || !p->ready || !p->is_drum || !a || kit>254u)return BB_FACTORY_PATTERN_INVALID;
#if !BB_WITH_RICH_DRUM_BROWSER || !BB_SAMPLE_HOST_INTEGRATION
    (void)kit;return BB_FACTORY_PATTERN_UNAVAILABLE;
#else
    if(p->target>=BB_RICH_DRUMS || a->drum_patterns.family[p->target])return BB_PLAYER_NEEDS_KIT;
    const bb_browser selected=p->view;
    return launch(p,a,&selected,(int)kit);
#endif
}
