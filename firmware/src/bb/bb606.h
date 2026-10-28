/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB606_H
#define BB606_H
#include <stddef.h>
#include <stdint.h>
#ifndef BB606_PREFIX
#define BB606_PREFIX bb606
#endif
#define BB606_JOIN_(a,b) a##b
#define BB606_JOIN(a,b) BB606_JOIN_(a,b)
#define BB606_API(n) BB606_JOIN(BB606_PREFIX,n)
#ifdef __cplusplus
extern "C" {
#endif
enum {BB606_BD,BB606_SD,BB606_LT,BB606_HT,BB606_CH,BB606_OH,BB606_CY,BB606_VOICES};
/* Decay 0..1; tune is semitones [-12,12] for BD, ratio [.25,4] otherwise.
 * All configuration/trigger/render writes require exclusive audio ownership.
 * No heap, I/O, duplicated FX, string parameter parsing or hidden limiter. */
typedef struct {float tune,decay,level,echo,spring,attack,snappy,tone;} bb606_config;
size_t BB606_API(_size)(void);
size_t BB606_API(_alignment)(void);
int BB606_API(_init)(void *,size_t,float);
int BB606_API(_default)(unsigned,bb606_config *);
int BB606_API(_configure)(void *,unsigned,const bb606_config *);
int BB606_API(_trigger)(void *,unsigned,unsigned);
int BB606_API(_render)(void *,float *,float *,float *,unsigned);
uint32_t BB606_API(_active)(const void *);
#ifdef __cplusplus
}
#endif
#endif
