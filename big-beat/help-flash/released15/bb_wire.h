/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
uint32_t bb_wire_get32(const uint8_t *);
void bb_wire_put32(uint8_t *,uint32_t);
int bb_wire_overlap(const void *,size_t,const void *,size_t);
int bb_wire_separated(const void *,size_t,const void *,size_t);
