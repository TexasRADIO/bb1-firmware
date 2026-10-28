/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "group_gain.h"
#if !defined(BB_GROUP_909_HOST_TEST) && !defined(BB_GROUP_TARGET_CANDIDATE)
#error "Group actor awaits HAL ownership integration and target budget verification"
#endif
#define BB909_OWNER_STATE 7680u
#define BB909_OWNER_EVENTS 32u
#define BB909_OWNER_FRAMES 128u
enum {BB909_EVENT_SET,BB909_EVENT_TRIGGER};
typedef struct {uint16_t offset;uint8_t kind,voice,parameter,value;} bb909_event;
typedef struct {
    uint32_t ticket,frames,count;
    bb909_event events[BB909_OWNER_EVENTS];
    /* Compact target controls, not floating-point snapshots or live pointers. */
    struct {uint8_t level,muted;} groups[GG_COUNT];
    uint32_t check;
} bb909_request;
typedef union {uint64_t alignment;unsigned char bytes[BB909_OWNER_STATE];} bb909_owner_state;
typedef struct {
    uint32_t guard,last_ticket,ready,failed,frames;
    bb909_owner_state state;
    float bus[4][BB909_OWNER_FRAMES];
    gg_state groups;
    uint32_t end_guard;
} bb909_owner;
/* Actor and request must be fixed firmware buffers, never chosen by mailbox
 * data. A single worker exclusively owns DSP state. CPU0 may read output only
 * after checked transport completion, and may reset only after confirmed hold.
 * Init/asset validation are pre-audio, not a realtime kit-change operation. */
int bb909_owner_init(bb909_owner *,uint32_t,const void *,size_t);
uint32_t bb909_request_check(const bb909_request *);
int bb909_owner_run(bb909_owner *,const bb909_request *,uint32_t,uint32_t *);
int bb909_owner_accept(const bb909_owner *,uint32_t,uint32_t);

_Static_assert(sizeof(((bb909_request *)0)->groups)==8,"group request must be eight bytes");
