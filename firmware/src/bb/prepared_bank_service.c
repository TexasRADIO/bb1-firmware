/* SPDX-License-Identifier: GPL-3.0-only */
#include "prepared_bank_service.h"
int pb_service_one_split(prepared_bank *bank,unsigned rate,
    bb_enter_fn enter,bb_leave_fn leave,void *lock_context,
    int (*allow)(void *),void *allow_context) {
    if(!bank||!enter||!leave||!allow||rate<8000u||rate>192000u)return PB_SERVICE_INVALID;
    uint32_t ticket=0;unsigned slot=0;int kind=0;
    uintptr_t token=enter(lock_context);
    if(allow(allow_context)) {
        if(pb_refill_claim(bank,&ticket,&slot))kind=1;
        else if(pb_part_claim(bank,&ticket))kind=2;
        else if(pb_seed_claim(bank,&ticket,&slot))kind=3;
    }
    leave(lock_context,token);
    if(!kind)return PB_SERVICE_NONE;
    int valid=kind==1?pb_refill_expand(bank,ticket,slot):
        kind==2?pp_expand(&bank->parts,ticket,rate):pb_seed_copy(bank,ticket,slot);
    token=enter(lock_context);
    int accepted=kind==1?pb_refill_finish(bank,ticket,slot,valid):
        kind==2?pp_finish(&bank->parts,ticket,valid):pb_seed_finish(bank,ticket,slot,valid);
    leave(lock_context,token);
    if(!valid)return PB_SERVICE_FAILED;
    if(!accepted)return PB_SERVICE_DISCARDED;
    return kind==1?PB_SERVICE_REFILLED:kind==2?PB_SERVICE_PART_READY:PB_SERVICE_SEEDED;
}
int pb_service_one(prepared_bank *bank,unsigned rate,
    bb_enter_fn enter,bb_leave_fn leave,int (*allow)(void *),void *context) {
    return pb_service_one_split(bank,rate,enter,leave,context,allow,context);
}
