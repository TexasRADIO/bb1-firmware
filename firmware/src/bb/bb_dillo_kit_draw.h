/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_DILLO_KIT_DRAW_H
#define BB_DILLO_KIT_DRAW_H
#include "bb_dillo_activity.h"
typedef void (*bb_dillo_line)(void *,int,int,int,int);
/* Fixed 34x24 logical bounds. Integer geometry, no trig, allocation, sprites
 * or audio access. Caller clips/translates and supplies resolved voice label.
 * Motion level 0..4; reduced motion uses 0 while activity remains in its label. */
void bb_dillo_kit_draw(unsigned pose,unsigned level,int x,int y,bb_dillo_line,void *);
#endif
