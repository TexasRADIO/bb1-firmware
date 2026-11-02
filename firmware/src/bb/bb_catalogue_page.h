/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_CATALOGUE_PAGE_H
#define BB_CATALOGUE_PAGE_H
#include <stddef.h>
#include <stdint.h>
enum {BB_PAGE_RAW=0,BB_PAGE_LZ4=1,BB_PAGE_DEFLATE_FIXED=2,BB_PAGE_MAX=4096,BB_PAGE_ENCODED_MAX=4128};
/* Control/startup-thread page decoding into caller-owned PRIVATE workspace.
 * No heap/global scratch. Input/output ranges must be mapped, disjoint and
 * live for the call. Range checks cannot prove arbitrary pointer ownership.
 * Success consumes all encoded bytes and writes exactly expected bytes.
 * Argument refusals precede writes; malformed data may leave partial output.
 * On any failure discard the workspace contents; never target live music.
 * This is an extent-safe LZ4 block decoder, not an integrity/authentication
 * check or a catalogue validator. Header/CRC/schema validation remains separate.
 * It accepts decodable final literal sequences, not only our encoder's choices.
 */
/* Shared range helpers of the catalogue readers: p is non-null and p+n does not wrap; [a,a+an) and [b,b+bn)
 * do not overlap. */
int bb_catalogue_page_span(const void *,size_t);
int bb_catalogue_page_disjoint(const void *,size_t,const void *,size_t);
int bb_catalogue_page_decode(void *output,size_t capacity,const void *input,
    size_t encoded,size_t expected,unsigned codec);
#endif
