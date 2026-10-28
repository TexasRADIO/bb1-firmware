/* SPDX-License-Identifier: GPL-3.0-only */
/* Host model dispatch; not the target HAL entrypoint. */
#include "owner-model.h"
#include "ordered.h"
static int plc_guard(uint32_t g) {
    return g==PO_GUARD||g==PC_GUARD||g==PA_GUARD||g==PA_CONTROL_GUARD;
}
static int plc_valid(const po_request *r) {
    return plc_guard(r->guard)&&r->check==bb_c1_drum_hash(r->guard,r,offsetof(po_request,check));
}
static __attribute__((noinline)) int plc_run(po_owner *o,const po_request *r,po_result *result) {
    if(r->guard==PO_GUARD)return po_run(o,r,result);
    if(r->guard==PC_GUARD)return pc_run(o,(const pc_request *)(const void *)r,result);
    if(r->guard==PA_GUARD)return pa_run(o,(const pa_request *)(const void *)r,result);
    if(r->guard==PA_CONTROL_GUARD)return pa_control_run(o,(const pa_request *)(const void *)r,result);
    return 0;
}
static int pla_accept(po_owner *o,const po_result *r,uint32_t t,unsigned k,unsigned f,
                      uint32_t *state,float out[2][3][128]) {
    return f?po_accept(o,r,t,k,f,state,out):pa_control_accept(o,r,t,k,state);
}
#define BB_PL_REQUEST_VALID(r) plc_valid(r)
#define BB_PL_JOB_GUARD_VALID(seed) plc_guard(seed)
#define BB_PL_RUN(o,r,result) plc_run(o,r,result)
#define BB_PL_FRAMES_VALID(r) ((r)->guard==PA_CONTROL_GUARD?(r)->frames==0:((r)->frames&&(r)->frames<=128u))
#define BB_PL_ACCEPT(o,r,t,k,f,s,out) pla_accept(o,r,t,k,f,s,out)
#include "lease-checked.h"
static int plc_publish(po_lease *l,const pc_request *r) {
    if(!l||!r||!bb_rt_separate(l,sizeof *l,r,sizeof *r))return 0;
    po_request raw;__builtin_memcpy(&raw,r,sizeof raw);return pl_publish(l,&raw);
}
