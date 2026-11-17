/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-qualified protocol dispatch, NOT the target fixed-entry dispatcher.
 * Normal/raw and compact/preset requests share the same immutable lease wire.
 * The mailbox seed must agree with the request's version guard. Never infer
 * the format from size alone. Includes the checked collection/export gates.
 */
#include "owner-model.h"
#include "compact.h"
_Static_assert(sizeof(pc_request)==sizeof(po_request),"shared wire size");
_Static_assert(offsetof(pc_request,check)==offsetof(po_request,check),"shared checksum position");
_Static_assert(offsetof(pc_request,payload)==offsetof(po_request,commands),"shared header");
static int plc_guard(uint32_t guard) {return guard==PO_GUARD||guard==PC_GUARD;}
static int plc_valid(const po_request *r) {
    return plc_guard(r->guard)&&r->check==bb_c1_drum_hash(r->guard,r,offsetof(po_request,check));
}
static int plc_run(po_owner *o,const po_request *r,po_result *result) {
    if(r->guard==PO_GUARD)return po_run(o,r,result);
    /* pc_run uses the input only as byte-copy source, not typed lvalue reads.
     * Its own correctly typed local request is validated before any mutation.
     */
    if(r->guard==PC_GUARD)return pc_run(o,(const pc_request *)(const void *)r,result);
    return 0;
}
#define BB_PL_REQUEST_VALID(r) plc_valid(r)
#define BB_PL_JOB_GUARD_VALID(seed) plc_guard(seed)
#define BB_PL_RUN(o,r,result) plc_run(o,r,result)
#include "lease-checked.h"
static int plc_publish(po_lease *l,const pc_request *r) {
    if(!l||!r||!bb_rt_separate(l,sizeof *l,r,sizeof *r))return 0;
    /* CPU0 local conversion avoids accessing a pc_request as po_request.
     * This extra540B stack frame is explicitly NOT target qualified.
     */
    po_request raw;__builtin_memcpy(&raw,r,sizeof raw);return pl_publish(l,&raw);
}
