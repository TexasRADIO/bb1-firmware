/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909.h"
#include "drum909.h"
#include "bb909_assets.h"
#include "pack_constants.h"
#include <string.h>
#define BB909_MAGIC UINT32_C(0x42423939)
typedef struct { uint32_t magic; drum909_t dsp; } bb909_state;
size_t bb909_size(void) { return sizeof(bb909_state); }
size_t bb909_alignment(void) { return _Alignof(bb909_state); }
static int aligned(const void *p, size_t n) { return p && (uintptr_t)p % n == 0; }
static int valid(const void *p) {
    return aligned(p, _Alignof(bb909_state)) && ((const bb909_state *)p)->magic == BB909_MAGIC;
}
static int overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}
int bb909_init(void *p, size_t n, unsigned rate) {
#ifdef BB909_EXTERNAL_PCM
    (void)p; (void)n; (void)rate;
    return 0; /* No fabricated or empty PCM when an asset pack is required. */
#else
    if (!aligned(p, _Alignof(bb909_state)) || n < sizeof(bb909_state) || rate != 44100) return 0;
    memset(p, 0, sizeof(bb909_state));
    drum909_init(&((bb909_state *)p)->dsp);
    ((bb909_state *)p)->magic = BB909_MAGIC;
    return 1;
#endif
}
int bb909_init_pack(void *p, size_t n, unsigned rate, const void *image, size_t bytes) {
    drum909_samples_t samples;
#ifdef BB909_EXTERNAL_TABLES
    /* The linked lookup symbols name this exact XIP pack, not any valid
     * buffer elsewhere. Refuse a mismatched asset image before DSP init. */
    if ((uintptr_t)image != BB909_PACK_XIP_ADDR) return 0;
#endif
    if (!aligned(p, _Alignof(bb909_state)) || n < sizeof(bb909_state) || rate != 44100
        || !image || overlap(p, sizeof(bb909_state), image, bytes)
        || !bb909_assets_parse(image, bytes, &samples)) return 0;
    memset(p, 0, sizeof(bb909_state));
    drum909_init_samples(&((bb909_state *)p)->dsp, &samples);
    ((bb909_state *)p)->magic = BB909_MAGIC;
    return 1;
}
int bb909_set(void *p, unsigned voice, unsigned parameter, unsigned value) {
    if (!valid(p) || voice > DR_KIT || parameter >= (unsigned)drum909_nparams((int)voice)) return 0;
    const x0x_param_t *spec = drum909_param((int)voice, (int)parameter);
    if (!spec || value > spec->max) return 0;
    drum909_set(&((bb909_state *)p)->dsp, (int)voice, (int)parameter, (int)value);
    return 1;
}
int bb909_trigger(void *p, unsigned voice, unsigned velocity) {
    if (!valid(p) || voice >= DR_NUM || velocity > 127) return 0;
    if (velocity) drum909_trigger(&((bb909_state *)p)->dsp, (int)voice, (float)velocity / 127.0f);
    return 1;
}
int bb909_active(const void *p) {
    return valid(p) && drum909_active(&((const bb909_state *)p)->dsp);
}
int bb909_render(void *p, float *l, float *r, float *e, float *s, unsigned n) {
    if (!valid(p) || !n || n > 128 || !aligned(l, _Alignof(float))
        || !aligned(e, _Alignof(float)) || !aligned(s, _Alignof(float))
        || (r && !aligned(r, _Alignof(float)))) return 0;
    float *bus[4] = {l, r, e, s};
    size_t bytes = n * sizeof(float);
    for (unsigned i = 0; i < 4; ++i) if (bus[i]) {
        if (overlap(p, sizeof(bb909_state), bus[i], bytes)) return 0;
        for (unsigned j = i + 1; j < 4; ++j)
            if (bus[j] && overlap(bus[i], bytes, bus[j], bytes)) return 0;
    }
    for (unsigned i = 0; i < 4; ++i) if (bus[i]) memset(bus[i], 0, bytes);
    drum909_t *d = &((bb909_state *)p)->dsp;
    if (r) drum909_render_st(d, l, r, s, e, (int)n);
    else drum909_render(d, l, s, e, (int)n);
    for (unsigned i = 0; i < 4; ++i) if (bus[i])
        for (unsigned k = 0; k < n; ++k) if (!__builtin_isfinite(bus[i][k])) return 0;
    return 1;
}

#include "group_validate.h"
void proposal909_render_st(drum909_t *,gg_state *,float *,float *,float *,float *,int);
int bb909_render_groups(void *p, gg_state *groups, float *l, float *r, float *e, float *s, unsigned n) {
    if(!aligned(groups,_Alignof(gg_state))||!gg_valid(groups)
       ||overlap(p,sizeof(bb909_state),groups,sizeof *groups))return 0;
    if (!valid(p) || !n || n > 128 || !aligned(l, _Alignof(float))
        || !aligned(e, _Alignof(float)) || !aligned(s, _Alignof(float))
        || (r && !aligned(r, _Alignof(float)))) return 0;
    float *bus[4] = {l, r, e, s};
    size_t bytes = n * sizeof(float);
    for (unsigned i = 0; i < 4; ++i) if (bus[i]) {
        if (overlap(p, sizeof(bb909_state), bus[i], bytes)||overlap(groups,sizeof *groups,bus[i],bytes)) return 0;
        for (unsigned j = i + 1; j < 4; ++j)
            if (bus[j] && overlap(bus[i], bytes, bus[j], bytes)) return 0;
    }
    for (unsigned i = 0; i < 4; ++i) if (bus[i]) memset(bus[i], 0, bytes);
    drum909_t *d = &((bb909_state *)p)->dsp;
    proposal909_render_st(d,groups,l,r,s,e,(int)n);
    for (unsigned i = 0; i < 4; ++i) if (bus[i])
        for (unsigned k = 0; k < n; ++k) if (!__builtin_isfinite(bus[i][k])) return 0;
    return 1;
}
