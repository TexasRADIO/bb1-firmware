/* SPDX-License-Identifier: GPL-3.0-only
 * Diagnostic only. Arm once before audio starts; only the private worker kit
 * may write the record. Read it only after the worker has been held. */
#ifndef BB_COEFFICIENT_TRACE_H
#define BB_COEFFICIENT_TRACE_H
#include <stdint.h>
typedef struct {
 uintptr_t kit;
 volatile uint32_t stage,frame,expected,observed;
} bb_coefficient_record;
extern bb_coefficient_record bb_coefficient_trace;
int bb_coefficient_check(const void *kit,unsigned stage,unsigned frame);
#endif
