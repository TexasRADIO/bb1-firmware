#include "group_gain.h"
#include "drum808.c"
#include "coefficient_trace.h"
bb_coefficient_record bb_coefficient_trace;
__attribute__((noinline)) int bb_coefficient_check(const void *kit,unsigned stage,unsigned frame)
{
 if((uintptr_t)kit!=bb_coefficient_trace.kit)return 0;
 if(bb_coefficient_trace.stage)return 1;
 const drum808_t *d=kit;
 uint32_t observed,expected;float constant=D8_HP1_A_200;
 __builtin_memcpy(&observed,&d->cb.hp.a,4);
 __builtin_memcpy(&expected,&constant,4);
 if(observed==expected)return 0;
 bb_coefficient_trace.frame=frame;bb_coefficient_trace.expected=expected;
 bb_coefficient_trace.observed=observed;bb_coefficient_trace.stage=stage;
 return 1;
}
#include "group_renderer_generated.inc"
#include "metal_capture.inc"
