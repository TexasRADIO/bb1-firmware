/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 review adapter for exact SLOOP v2.8.3 recipes; no flash writer. */
#ifndef BB1_SLOOP_BANK_H
#define BB1_SLOOP_BANK_H
#include <stddef.h>
#include <stdint.h>
#define SL_BANK_BYTES 9216u
#define SL_BANK_RECIPE_CRC 0xe1f31560u
typedef struct {const uint8_t *bytes;uint16_t records;} sl_bank;
/* bind validates the complete image; a refused bind leaves the output unchanged.
 * Caller retains immutable image lifetime until every referencing voice is silent. */
int sl_bank_bind(sl_bank *,const void *,size_t);
/* Stable direct record bytes; exact22-byte sl_sound layout. NULL on bad indices. */
const uint8_t *sl_bank_sound(const sl_bank *,unsigned kit,unsigned lane);
int sl_bank_crush(const sl_bank *,unsigned kit,unsigned *out);
#endif
