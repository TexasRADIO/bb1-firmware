/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* IEEE CRC32 accumulator; caller supplies the seed and final inversion.
 * Valid immutable bytes only, outside live audio. NULL is allowed for n=0. */
uint32_t bb_crc32_add(uint32_t,const uint8_t *,size_t);
