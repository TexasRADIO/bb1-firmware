/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_factory_trio_launch.h"
#include "bb_audio_family.h"
#if BB_WITH_PARTS
static int separated(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return a && b && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int inputs(bb_audio *a,const bb_catalogue *c,const bb_factory_choice *choice,
                  const bb_core *inherited,bb_factory_trio_workspace *work,
                  bb_factory_trio_report *report,uint32_t *ticket) {
    const void *regions[]={a,c,choice,inherited,work,report,ticket,c->container.bytes,c->container.cache};
    const size_t sizes[]={sizeof *a,sizeof *c,sizeof *choice,sizeof *inherited,
                         sizeof *work,sizeof *report,sizeof *ticket,c->container.length,sizeof *c->container.cache};
    for(unsigned i=0;i<9;i++)for(unsigned j=0;j<i;j++)
        if(!separated(regions[i],sizes[i],regions[j],sizes[j]))return 0;
#if BB_TRIO_EXTERNAL_PROGRAM
    const bb_trio_program *program=a->trio.program;
#else
    const bb_trio_program *program=&a->trio.program;
#endif
    if(program)for(unsigned i=1;i<9;i++)
        if(!separated(program,sizeof *program,regions[i],sizes[i]))return 0;
    return 1;
}
#endif
int bb_factory_request_trio(bb_audio *a,const bb_catalogue *c,const bb_factory_choice *choice,
    const bb_core *inherited,unsigned primary,unsigned policy,unsigned phrase,
    bb_factory_trio_workspace *work,bb_factory_trio_report *report,uint32_t *ticket) {
    if(!a || !c || !choice || !inherited || !work || !report || !ticket)return BB_FTRIO_INVALID;
#if BB_WITH_PARTS
    if(primary>=BB_MACHINES || policy&~BB_FTRIO_THREE_ENGINE || phrase<16 || phrase>64 || phrase%16 ||
       (!a->commands.enter != !a->commands.leave) || !inputs(a,c,choice,inherited,work,report,ticket))
        return BB_FTRIO_INVALID;
#if BB_TRIO_EXTERNAL_PROGRAM
    if(!a->trio.program)return BB_FTRIO_REQUEST_UNAVAILABLE;
#endif
    /* This source assembler is the legacy 808 lane profile. Do not reinterpret
     * a 909 or another family's stored lane bits, even in an inactive layer.
     * The consumer's original publish/apply guards remain in place too. */
    uintptr_t token=a->commands.enter?a->commands.enter(a->commands.lock_context):0;
    const uint8_t *family=bb_audio_families(a);unsigned unsupported=0;
    for(unsigned i=0;i<BB_MACHINES;i++)unsupported|=family[i]!=0;
    if(a->commands.leave)a->commands.leave(a->commands.lock_context,token);
    if(unsupported)return BB_FTRIO_FAMILY;
    if(!inherited->drums[primary].enabled)return BB_FTRIO_REQUEST_OFF;
    bb_trio_program *owned=NULL;uint32_t accepted=0;
    if(!bb_audio_trio_begin(a,&owned,&accepted))return BB_FTRIO_REQUEST_BUSY;
    int result=bb_factory_prepare_trio(c,choice,inherited,primary,policy,owned,work,report);
    if(result!=BB_FTRIO_OK) {
        (void)bb_audio_trio_abort(a,accepted);
        return result;
    }
    if(!bb_audio_trio_publish(a,accepted,phrase))return BB_FTRIO_REQUEST_BUSY;
    *ticket=accepted;
    return BB_FTRIO_OK;
#else
    (void)primary;(void)policy;(void)phrase;
    return BB_FTRIO_REQUEST_UNAVAILABLE;
#endif
}
