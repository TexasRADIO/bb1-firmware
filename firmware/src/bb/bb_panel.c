#include "bb_panel.h"
#include <string.h>
void bb_panel_init(bb_panel *p,const bb_core *c) {
    memset(p,0,sizeof *p);p->requested=*c;p->animate=1;
}
int bb_panel_sync_patterns_mask(bb_panel *p,bb_commands *q,const bb_core *c,unsigned mask) {
    unsigned changed=0;uintptr_t token;
    if(!p || !q || !c || (!q->enter != !q->leave))return 0;
    if(mask&~((1u<<(BB_MACHINES+BB_SYNTHS))-1u))return 0;
    if(!mask)return 1;
    token=q->enter?q->enter(q->lock_context):0;
    if(!bb_commands_idle(q)){if(q->leave)q->leave(q->lock_context,token);return 0;}
    for(unsigned k=0;k<BB_MACHINES;k++)if(mask&(1u<<k)) {
        bb_machine *to=&p->requested.drums[k];const bb_machine *from=&c->drums[k];
        changed|=to->length!=from->length || memcmp(to->steps,from->steps,sizeof to->steps)!=0;
        to->length=from->length;memcpy(to->steps,from->steps,sizeof to->steps);
        /* Factory patterns can commit required voice modes with the notes.
         * Keep the knob/display mirror aligned, without copying other pots. */
        changed|=memcmp(to->sound.mode,from->sound.mode,sizeof to->sound.mode)!=0;
        memcpy(to->sound.mode,from->sound.mode,sizeof to->sound.mode);
        to->sound_revision=from->sound_revision;
    }
    for(unsigned k=0;k<BB_SYNTHS;k++)if(mask&(1u<<(BB_MACHINES+k))) {
        bb_sequence *to=&p->requested.synths[k];const bb_sequence *from=&c->synths[k];
        changed|=to->length!=from->length || memcmp(to->notes,from->notes,sizeof to->notes)!=0;
        to->length=from->length;memcpy(to->notes,from->notes,sizeof to->notes);
    }
    if(q->leave)q->leave(q->lock_context,token);
    return changed?2:1;
}
int bb_panel_sync_patterns(bb_panel *p,bb_commands *q,const bb_core *c) {
    return bb_panel_sync_patterns_mask(p,q,c,(1u<<(BB_MACHINES+BB_SYNTHS))-1u);
}
int bb_panel_send(bb_panel *p,bb_commands *audio,const bb_command *c) {
    bb_commands mirror;
    if(!bb_command_push(audio,c)){p->rejected++;return 0;}
    /* Use the identical semantics, including validation, for the UI mirror.
     * This temporary queue has no other reader/writer and runs in the main loop. */
    bb_commands_init(&mirror,0,0,0);
    bb_command_push(&mirror,c);bb_apply_pending(&p->requested,&mirror);
    p->rejected=0;return 1;
}
int bb_panel_drum_hit(bb_panel *p,bb_commands *audio,unsigned voice,unsigned velocity) {
    bb_command c={0};
    if(!p || p->machine>=BB_MACHINES || voice>=BB_DRUM_TRACKS || !velocity || velocity>127)return 0;
    c.type=BB_CMD_DRUM_TRIGGER;c.target=p->machine;c.voice=(uint8_t)voice;c.value=(uint16_t)velocity;
    return bb_panel_send(p,audio,&c);
}
int bb_panel_step(bb_panel *p,bb_commands *audio,unsigned local) {
    unsigned step;
    bb_machine *m;bb_command c={0};uint16_t bit;
    if(!p || !audio || local>=16 || p->machine>=BB_MACHINES || p->voice>=11)return 0;
    step=(unsigned)p->bank*16+local;
    m=&p->requested.drums[p->machine];if(step>=m->length)return 0;
    bit=(uint16_t)(1u<<p->voice);
    c.type=BB_CMD_DRUM_TOGGLE;c.target=p->machine;c.voice=p->voice;c.step=(uint8_t)step;
    if(p->accent) {
        if(!(m->steps[step].hits&bit))return 0;
        c.value=1;
    } else c.value=0;
    return bb_panel_send(p,audio,&c);
}
static int black(unsigned k) {return (int)((0x54au>>((k+5u)%12u))&1u);}
int bb_panel_synth_step(bb_panel *p,bb_commands *q,unsigned synth,unsigned local,bb_edit edit,unsigned pitch) {
    bb_command c={0};unsigned step;
    if(!p || synth>=BB_SYNTHS || local>=16 || pitch>127 || edit>BB_SLIDE || edit<BB_GATE)return 0;
    step=(unsigned)p->bank*16u+local;if(step>=p->requested.synths[synth].length)return 0;
    c.type=BB_CMD_NOTE_EDIT;c.target=(uint8_t)synth;c.step=(uint8_t)step;
    c.voice=(uint8_t)edit;c.value=(uint16_t)pitch;
    if(edit!=BB_GATE && !p->requested.synths[synth].notes[step].gate)return 0;
    return bb_panel_send(p,q,&c);
}
int bb_panel_synth_pitch(bb_panel *p,bb_commands *q,unsigned synth,unsigned step,unsigned pitch) {
    bb_command c={0};
    if(!p || synth>=BB_SYNTHS || step>=p->requested.synths[synth].length || pitch>127)return 0;
    c.type=BB_CMD_NOTE_PITCH;c.target=(uint8_t)synth;c.step=(uint8_t)step;c.value=(uint16_t)pitch;
    return bb_panel_send(p,q,&c);
}
int bb_panel_synth_shift(bb_panel *p,bb_commands *q,unsigned synth,unsigned step,int delta) {
    bb_command c={0};
    if(!p || synth>=BB_SYNTHS || step>=p->requested.synths[synth].length || delta<-127 || delta>127)return 0;
    c.type=BB_CMD_NOTE_SHIFT;c.target=(uint8_t)synth;c.step=(uint8_t)step;c.value=(uint16_t)(delta+127);
    return bb_panel_send(p,q,&c);
}
static int key_index(unsigned k,int wanted) {
    unsigned i;int n=0;
    if(k>=27 || black(k)!=wanted)return -1;
    for(i=0;i<k;i++)if(black(i)==wanted)n++;
    return n;
}
int bb_panel_white_step(unsigned k) {return key_index(k,0);}
int bb_panel_black_index(unsigned k) {return key_index(k,1);}
