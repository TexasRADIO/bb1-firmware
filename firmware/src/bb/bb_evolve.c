/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_evolve.h"
#include <string.h>

static int valid_patterns(const bb_core *c) {
    if(!c)return 0;
    for(unsigned p=0;p<BB_MACHINES;p++)if(!c->drums[p].length || c->drums[p].length>BB_STEPS)return 0;
    for(unsigned p=0;p<BB_SYNTHS;p++) {
        if(!c->synths[p].length || c->synths[p].length>BB_STEPS)return 0;
        for(unsigned s=0;s<BB_STEPS;s++) {
            const bb_note *n=&c->synths[p].notes[s];
            if(n->note>127 || n->gate>1 || n->accent>1 || n->slide>1)return 0;
        }
    }
    return 1;
}
static int families_valid(const uint8_t *f) {
    if(!f)return 0;
    for(unsigned i=0;i<BB_MACHINES;i++)if(f[i]>1)return 0;
    return 1;
}
static void snapshot(bb_pattern_set *p,const bb_core *c,const uint8_t *family) {
    memcpy(p->family,family,BB_MACHINES);
    for(unsigned m=0;m<BB_MACHINES;m++) {
        memcpy(p->drums[m],c->drums[m].steps,sizeof p->drums[m]);p->length[m]=c->drums[m].length;
    }
    for(unsigned m=0;m<BB_SYNTHS;m++) {
        memcpy(p->synths[m],c->synths[m].notes,sizeof p->synths[m]);p->length[BB_MACHINES+m]=c->synths[m].length;
    }
}
static int matches(const bb_pattern_set *p,const bb_core *c) {
    for(unsigned m=0;m<BB_MACHINES;m++)if(p->length[m]!=c->drums[m].length || memcmp(p->drums[m],c->drums[m].steps,sizeof p->drums[m]))return 0;
    for(unsigned m=0;m<BB_SYNTHS;m++)if(p->length[BB_MACHINES+m]!=c->synths[m].length || memcmp(p->synths[m],c->synths[m].notes,sizeof p->synths[m]))return 0;
    return 1;
}
static uint32_t random_next(bb_evolve *e) {
    uint32_t x=e->random;x^=x<<13;x^=x>>17;x^=x<<5;e->random=x;return x;
}
int bb_evolve_matches(const bb_evolve *e,const bb_core *c) {
    return e && e->ready && c && families_valid(e->current_family) &&
        families_valid(e->expected.family) &&
        !memcmp(e->current_family,e->expected.family,BB_MACHINES) && matches(&e->expected,c);
}
static int pitch_allowed(const bb_evolve_settings *s,unsigned note) {
    return note>=s->low_note && note<=s->high_note && (s->scale&(1u<<((note+12u-s->root)%12u)));
}
int bb_evolve_configure(bb_evolve *e,const bb_evolve_settings *s) {
    unsigned any=0;
    if(!e || !e->ready || !s || s->parts>=64 || s->attributes>=16 || s->amount>100 ||
       !s->every_phrases || s->every_phrases>64 || s->root>11 || !s->scale || s->scale>=4096 ||
       s->low_note>s->high_note || s->high_note>127 || s->drum_voices>=2048)return 0;
    for(unsigned n=s->low_note;n<=s->high_note;n++)any|=pitch_allowed(s,n);
    if(!any)return 0;
    e->settings=*s;memset(e->phrases,0,sizeof e->phrases);memset(e->seen,0,sizeof e->seen);return 1;
}
int bb_evolve_capture(bb_evolve *e,const bb_core *c) {
    if(!e || !e->ready || !valid_patterns(c) || !families_valid(e->current_family))return 0;
    snapshot(&e->home,c,e->current_family);e->expected=e->home;
    e->frozen=1;e->conflict=0;e->random=e->seed;e->changes=0;
    memset(e->phrases,0,sizeof e->phrases);memset(e->seen,0,sizeof e->seen);return 1;
}
int bb_evolve_init_families(bb_evolve *e,const bb_core *c,uint32_t seed,const uint8_t family[BB_MACHINES]) {
    if(!e || !valid_patterns(c) || !families_valid(family))return 0;
    uint8_t tags[BB_MACHINES];memcpy(tags,family,sizeof tags);
    memset(e,0,sizeof *e);memcpy(e->current_family,tags,sizeof tags);e->seed=seed?seed:0x42424556u;e->ready=1;
    e->settings.parts=63;e->settings.attributes=BB_EVOLVE_RHYTHM|BB_EVOLVE_PITCH|BB_EVOLVE_ACCENT;
    e->settings.amount=25;e->settings.every_phrases=8;e->settings.drum_voices=0x7fcu;
    e->settings.scale=0x5adu; /* natural minor, relative to root C */
    e->settings.low_note=24;e->settings.high_note=84;
    for(unsigned p=0;p<BB_EVOLVE_PARTS;p++)e->settings.locked[p]=1; /* home downbeat */
    return bb_evolve_capture(e,c); /* opt-in only, initially disabled/frozen */
}
int bb_evolve_init(bb_evolve *e,const bb_core *c,uint32_t seed) {
    static const uint8_t legacy[BB_MACHINES]={0};
    return bb_evolve_init_families(e,c,seed,legacy);
}
int bb_evolve_bind_families(bb_evolve *e,const uint8_t family[BB_MACHINES]) {
    if(!e || !e->ready || !families_valid(family))return 0;
    uint8_t tags[BB_MACHINES];memcpy(tags,family,sizeof tags);
    memcpy(e->current_family,tags,sizeof tags);
    if(memcmp(e->home.family,e->current_family,BB_MACHINES))e->conflict=e->frozen=1;
    return 1;
}
int bb_evolve_home(bb_evolve *e,bb_core *c) {
    if(!e || !e->ready || !valid_patterns(c) || !families_valid(e->current_family) ||
       !families_valid(e->home.family) || memcmp(e->home.family,e->current_family,BB_MACHINES))return 0;
    for(unsigned p=0;p<BB_EVOLVE_PARTS;p++)
        if(e->home.length[p]!=(p<BB_MACHINES?c->drums[p].length:c->synths[p-BB_MACHINES].length))return 0;
    for(unsigned p=0;p<BB_MACHINES;p++)memcpy(c->drums[p].steps,e->home.drums[p],sizeof e->home.drums[p]);
    for(unsigned p=0;p<BB_SYNTHS;p++)memcpy(c->synths[p].notes,e->home.synths[p],sizeof e->home.synths[p]);
    e->expected=e->home;e->random=e->seed;e->changes=0;e->conflict=0;e->frozen=1;
    memset(e->phrases,0,sizeof e->phrases);memset(e->seen,0,sizeof e->seen);return 1;
}
static unsigned changed_steps(const bb_evolve *e,const bb_core *c,unsigned part) {
    unsigned count=0;
    for(unsigned s=0;s<e->home.length[part];s++) {
        if(part<BB_MACHINES)count+=memcmp(&e->home.drums[part][s],&c->drums[part].steps[s],sizeof(bb_drum_step))!=0;
        else count+=memcmp(&e->home.synths[part-BB_MACHINES][s],&c->synths[part-BB_MACHINES].notes[s],sizeof(bb_note))!=0;
    }
    return count;
}
static void change_part(bb_evolve *e,bb_core *c,unsigned part) {
    const bb_evolve_settings *cfg=&e->settings;
    unsigned step=random_next(e)%e->home.length[part],flags=cfg->attributes;
    if(cfg->locked[part]&(UINT64_C(1)<<step))return;
    if(part<BB_MACHINES) {
        bb_drum_step *dst=&c->drums[part].steps[step],old=*dst;
        unsigned voice=random_next(e)%BB_DRUM_TRACKS,bit=1u<<voice;
        if(!(cfg->drum_voices&bit))return;
        unsigned action=random_next(e)&1u;
        if(action && (flags&BB_EVOLVE_ACCENT) && (dst->hits&bit))dst->accents^=(uint16_t)bit;
        else if(flags&BB_EVOLVE_RHYTHM)dst->hits^=(uint16_t)bit;
        if(changed_steps(e,c,part)>BB_EVOLVE_MAX_CHANGED_STEPS)*dst=old;
        if(memcmp(dst,&old,sizeof old))e->changes++;
        e->expected.drums[part][step]=*dst;
    } else {
        unsigned synth=part-BB_MACHINES;bb_note *dst=&c->synths[synth].notes[step],old=*dst;
        unsigned action=random_next(e)%4u;
        if(action==0 && (flags&BB_EVOLVE_RHYTHM))dst->gate^=1u;
        else if(action==1 && (flags&BB_EVOLVE_ACCENT) && dst->gate)dst->accent^=1u;
        else if(action==2 && (flags&BB_EVOLVE_SLIDE) && dst->gate && c->synths[synth].notes[(step+1u)%c->synths[synth].length].gate)dst->slide^=1u;
        else if(action==3 && (flags&BB_EVOLVE_PITCH) && dst->gate) {
            unsigned home=e->home.synths[synth][step].note,lo=home>7?home-7:0,hi=home<120?home+7:127,count=0;
            for(unsigned n=lo;n<=hi;n++)if(pitch_allowed(cfg,n))count++;
            if(count) {
                unsigned pick=random_next(e)%count;
                for(unsigned n=lo;n<=hi;n++)if(pitch_allowed(cfg,n) && pick--==0){dst->note=(uint8_t)n;break;}
            }
        }
        if(changed_steps(e,c,part)>BB_EVOLVE_MAX_CHANGED_STEPS)*dst=old;
        if(memcmp(dst,&old,sizeof old))e->changes++;
        e->expected.synths[synth][step]=*dst;
    }
}
void bb_evolve_before_tick(void *ctx,bb_core *c) {
    bb_evolve *e=ctx;
    if(!e || !c || !e->ready || !e->enabled || e->frozen || !e->settings.amount || !c->running)return;
    if(!bb_evolve_matches(e,c)){e->conflict=1;e->frozen=1;return;}
    for(unsigned p=0;p<BB_EVOLVE_PARTS;p++) {
        unsigned next=p<BB_MACHINES?c->drums[p].next:c->synths[p-BB_MACHINES].next;
        int audible=p<BB_MACHINES?c->drums[p].enabled && !c->drums[p].muted:!c->synths[p-BB_MACHINES].muted;
        if(!(e->settings.parts&(1u<<p)) || !audible || next)continue;
        if(!e->seen[p]){e->seen[p]=1;continue;} /* hear the original phrase first */
        if(++e->phrases[p]<e->settings.every_phrases)continue;
        e->phrases[p]=0;
        if(random_next(e)%100u<e->settings.amount)change_part(e,c,p);
    }
}
