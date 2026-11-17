/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* IEEE CRC32 accumulator; caller supplies the seed and final inversion.
 * Valid immutable bytes only, outside live audio. NULL is allowed for n=0. */
uint32_t bb_crc32_add(uint32_t,const uint8_t *,size_t);
/* The scene-record checksum shared by the scene codecs: ~CRC32 of bytes 0..11 and 16..n-1 of a record whose
 * bytes 12..15 hold it (n >= 16). */
uint32_t bb_crc32_record(const uint8_t *,size_t);
