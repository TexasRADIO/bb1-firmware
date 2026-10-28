/* SPDX-License-Identifier: GPL-3.0-only */
#include "group_input.h"

int bb_group_input(bb_commands *q,const gg_state *bank,unsigned machine,
                   unsigned action,unsigned group,int delta,bb_group_view *view) {
    bb_group_view next={{0},0,0};bb_command command={0};
    int result=BB_GROUP_REFUSED;
    if(!q || !bank || !view || machine>=BB_MACHINES || action>BB_GROUP_TOGGLE ||
       (action!=BB_GROUP_READ && group>=GG_COUNT) || !q->enter || !q->leave)return result;
    uintptr_t token=q->enter(q->lock_context);
    if(q->count>BB_COMMAND_CAPACITY || q->read>=BB_COMMAND_CAPACITY ||
       q->write>=BB_COMMAND_CAPACITY || q->stop_pending || q->stop_inflight)goto done;
    if(q->write!=(q->read+q->count)%BB_COMMAND_CAPACITY)goto done;
    for(unsigned g=0;g<GG_COUNT;g++) {
        const gg_fader *f=&bank[machine].fader[g];
        if(f->level>127 || f->muted>1)goto done;
        next.level[g]=f->level;next.muted|=(uint8_t)(f->muted<<g);
    }
    /* At most the existing sixteen commands; no unbounded FIFO traversal. */
    for(unsigned i=0;i<q->count;i++) {
        const bb_command *c=&q->items[(q->read+i)%BB_COMMAND_CAPACITY];
        if(c->type!=BB_CMD_GROUP_LEVEL && c->type!=BB_CMD_GROUP_MUTE)continue;
        if(c->target>=BB_MACHINES || c->voice>=GG_COUNT || c->step ||
           c->note.note || c->note.gate || c->note.accent || c->note.slide ||
           c->value>(c->type==BB_CMD_GROUP_LEVEL?127u:1u))goto done;
        if(c->target!=machine)continue;
        unsigned bit=1u<<c->voice;
        if(c->type==BB_CMD_GROUP_LEVEL)next.level[c->voice]=(uint8_t)c->value;
        else next.muted=(uint8_t)((next.muted&~bit)|(c->value?bit:0));
        next.pending|=(uint8_t)bit;
    }
    result=BB_GROUP_UNCHANGED;
    if(action==BB_GROUP_READ)goto accepted;
    unsigned value;
    if(action==BB_GROUP_TURN) {
        if(delta>127)delta=127;
        if(delta< -127)delta= -127;
        int changed=(int)next.level[group]+delta;
        value=(unsigned)(changed<0?0:changed>127?127:changed);
        if(value==next.level[group])goto accepted;
        command.type=BB_CMD_GROUP_LEVEL;
    } else {
        value=!(next.muted&(1u<<group));command.type=BB_CMD_GROUP_MUTE;
    }
    if(q->count==BB_COMMAND_CAPACITY){result=BB_GROUP_REFUSED;goto done;}
    command.target=(uint8_t)machine;command.voice=(uint8_t)group;command.value=(uint16_t)value;
    /* Publish while the same guard still protects the snapshot. This mirrors
     * the validated FIFO append in bb_command_push without nesting IRQ hooks.
     * All command fields above obey its group-command validation contract. */
    q->items[q->write]=command;q->write=(q->write+1u)%BB_COMMAND_CAPACITY;q->count++;
    if(action==BB_GROUP_TURN)next.level[group]=(uint8_t)value;
    else next.muted=(uint8_t)((next.muted&~(1u<<group))|(value<<group));
    next.pending|=(uint8_t)(1u<<group);result=BB_GROUP_QUEUED;
accepted:
    *view=next;
done:
    q->leave(q->lock_context,token);return result;
}
