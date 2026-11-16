/* SPDX-License-Identifier: GPL-3.0-only */
/* New main-loop APIs only. Retain the original audio-trio consumer object. */
#include "bb_audio_trio.h"
#include "bb_wire.h"
#include "bb_panel.h"
#if BB_WITH_PARTS
static int hooks(const bb_audio *a){return a && (!a->commands.enter==!a->commands.leave);}
static uintptr_t enter(bb_audio *a){return a->commands.enter?a->commands.enter(a->commands.lock_context):0;}
static void leave(bb_audio *a,uintptr_t t){if(a->commands.leave)a->commands.leave(a->commands.lock_context,t);}
#if defined(__GNUC__) || defined(__clang__)
/* This source and destination are aligned bb_core objects, proven disjoint
 * before the guard is entered. may_alias permits copying object representation
 * without violating float/aggregate effective types. Do not replace global
 * memcpy: arbitrary unaligned buffers and flash/USB regions need its contract. */
typedef uint32_t bb_home_copy_word __attribute__((__may_alias__));
typedef char bb_home_copy_alignment[__alignof__(bb_core)>=__alignof__(bb_home_copy_word)?1:-1];
typedef char bb_home_copy_size[sizeof(bb_core)%sizeof(bb_home_copy_word)==0?1:-1];
static void bb_home_copy_core(bb_core *to,const bb_core *from) {
    bb_home_copy_word *destination=(bb_home_copy_word *)(void *)to;
    const bb_home_copy_word *source=(const bb_home_copy_word *)(const void *)from;
    for(size_t i=0;i<sizeof *to/sizeof *destination;i++)destination[i]=source[i];
}
#else
static void bb_home_copy_core(bb_core *to,const bb_core *from){*to=*from;}
#endif
static int output_safe(bb_audio *a,const void *out,size_t n) {
    if(!bb_wire_separated(a,sizeof *a,out,n))return 0;
#if BB_TRIO_EXTERNAL_PROGRAM
    if(a->trio.program && !bb_wire_separated(a->trio.program,sizeof *a->trio.program,out,n))return 0;
#endif
    return 1;
}
int bb_audio_performance_status(bb_audio *a,bb_performance_status *out) {
    if(!hooks(a) || !output_safe(a,out,sizeof *out))return 0;
    uintptr_t t=enter(a);
    bb_performance_status next={0};
    next.bank=a->trio.status;next.frame=a->core.frame;
#if BB_WITH_PREPARED_BANK
    next.part_launches=a->parts.parts.launches;next.bpm=a->core.bpm;
    next.ready=(uint8_t)pb_scenes_ready(&a->parts);next.active=a->parts.parts.active;
    next.queued=a->parts.parts.queued;next.position=a->parts.parts.position;
    next.phrase_steps=a->parts.parts.ready && next.active<BB_PART_COUNT?
        a->parts.parts.phrase_steps[next.active]:0;
#else
    next.part_launches=a->parts.launches;next.bpm=a->core.bpm;
    next.ready=a->parts.ready;next.active=a->parts.active;
    next.queued=a->parts.queued;next.position=a->parts.position;
    next.phrase_steps=next.ready && next.active<BB_PART_COUNT?a->parts.phrase_steps[next.active]:0;
#endif
    next.result=a->parts_result;next.error=a->parts_error;next.running=a->core.running;
    for(unsigned i=0;i<BB_MIX_CHANNELS;i++)next.level[i]=a->core.band_level[i];
    *out=next;leave(a,t);return 1;
}
int bb_audio_part_request(bb_audio *a,unsigned part) {
    if(!hooks(a) || part>=BB_PART_COUNT)return 0;
    uintptr_t t=enter(a);bb_commands *q=&a->commands;
#if BB_WITH_PREPARED_BANK
    if((!pb_scenes_ready(&a->parts) && !pb_seed_accepting(&a->parts)) ||
       a->trio.state!=BB_TRIO_FREE || q->count>=BB_COMMAND_CAPACITY ||
       q->write>=BB_COMMAND_CAPACITY || q->read>=BB_COMMAND_CAPACITY){leave(a,t);return 0;}
#else
    if(!a->parts.ready || a->trio.state!=BB_TRIO_FREE || q->count>=BB_COMMAND_CAPACITY ||
       q->write>=BB_COMMAND_CAPACITY || q->read>=BB_COMMAND_CAPACITY){leave(a,t);return 0;}
#endif
    bb_command c={0};c.type=BB_CMD_PART_LAUNCH;c.value=(uint16_t)part;
    q->items[q->write]=c;q->write=(q->write+1u)%BB_COMMAND_CAPACITY;q->count++;
    leave(a,t);return 1;
}
int bb_home_sync_panel(bb_panel *p,bb_audio *a) {
    if(!hooks(a) || !p || !bb_wire_separated(a,sizeof *a,p,sizeof *p))return 0;
#if BB_TRIO_EXTERNAL_PROGRAM
    if(a->trio.program && !bb_wire_separated(a->trio.program,sizeof *a->trio.program,p,sizeof *p))return 0;
#endif
    uintptr_t t=enter(a);
    if(!bb_commands_idle(&a->commands)){leave(a,t);return 0;}
    bb_page page=p->requested.page;bb_edit edit=p->requested.edit;
    uint8_t machine=p->requested.selected_machine,voice=p->requested.selected_voice;
    bb_home_copy_core(&p->requested,&a->core);p->requested.page=page;p->requested.edit=edit;
    p->requested.selected_machine=machine;p->requested.selected_voice=voice;
    unsigned length=page==BB_DRUMS?(p->machine<BB_MACHINES?p->requested.drums[p->machine].length:0):
        p->requested.synths[page==BB_TELEGRAM?1:0].length;
    if((unsigned)p->bank*16u>=length)p->bank=0;
    leave(a,t);return 1;
}
#else
int bb_audio_performance_status(bb_audio *a,bb_performance_status *s){(void)a;(void)s;return 0;}
int bb_audio_part_request(bb_audio *a,unsigned part){(void)a;(void)part;return 0;}
int bb_home_sync_panel(bb_panel *p,bb_audio *a){(void)p;(void)a;return 0;}
#endif
