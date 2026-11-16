/* SPDX-License-Identifier: GPL-3.0-only */
/* Startup/exclusively suspended ownership only. Platform phase release must
 * precede this call; this portable binding cannot verify MMIO/core hold. */
int bb_audio_groups_outside(const bb_audio *a,const void *p,size_t size) {
    return a && bb_wire_separated(a,sizeof *a,p,size)
        && (!a->groups || bb_wire_separated(a->groups,BB_MACHINES*sizeof(gg_state),p,size));
}
int bb_audio_groups_bind(bb_audio *a,gg_state *groups,unsigned count) {
    if(!a || a->core.sample_rate!=44100 || count!=BB_MACHINES ||
       ((uintptr_t)groups%__alignof__(gg_state)) ||
       (!a->commands.enter != !a->commands.leave) ||
       !bb_wire_separated(a,sizeof *a,groups,count*sizeof *groups))return 0;
    uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
    int okay=!a->groups && !a->core.running && !a->core.frame && bb_commands_idle(&a->commands);
#if BB_WITH_PARTS
    okay=okay && a->trio.state==BB_TRIO_FREE;
#if BB_WITH_PREPARED_BANK
    okay=okay && pb_ownership_idle(&a->parts);
#endif
#if BB_TRIO_EXTERNAL_PROGRAM
    okay=okay && a->trio.program && bb_wire_separated(a->trio.program,sizeof *a->trio.program,groups,count*sizeof *groups);
#endif
#endif
    if(okay){
        for(unsigned k=0;k<count;k++)gg_init(&groups[k]);
        a->groups=groups;
    }
    if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
    return okay;
}
