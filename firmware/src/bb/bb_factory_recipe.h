/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_FACTORY_RECIPE_H
#define BB_FACTORY_RECIPE_H
#include "bb_browser.h"
#include "bb_core.h"
enum {BB_RECIPE_OK,BB_RECIPE_INVALID,BB_RECIPE_FIELD,BB_RECIPE_RANGE,BB_RECIPE_MODEL};
enum {BB_RECIPE_GREYHOUND=1u,BB_RECIPE_HALF_SWING=2u,BB_RECIPE_SWING_RANGE=4u};
typedef struct {
    bb_dillo_patch dillo;
    bb_telegram_patch telegram;
    uint16_t bpm,swing_x2; /* exact50+sourceSwing/2 represented times two */
    uint8_t levels[BB_SYNTHS]; /* nearest source percent*127/100 */
    uint8_t capabilities; /* notices; not authorization to omit/round */
} bb_factory_recipe;
/* Read-only/control thread. Era sound recipes and explicit Studio loadEraSet
 * defaults, not a bank launch or permission to silently omit GreyHound.
 * Keep exact swing_x2 until the clock supports it; do not truncate odd values.
 * Known disabled advanced/arp/transpose settings are validated; unsupported
 * active modes and unknown fields reject. Current kit/FX/master are inherited
 * by Studio's era loader and deliberately NOT replaced here. Native controls
 * translate source settings, not circuit-exact Studio PCM. Output must be
 * distinct from catalogue/view/choice/data; unchanged on any failure. */
int bb_factory_read_recipe(const bb_catalogue *,const bb_factory_choice *,bb_factory_recipe *);
/* 1.5: a song bank's tempo and swing; 1 when present and in range. */
int bb_factory_read_song_tempo(const bb_catalogue *,const bb_factory_choice *,unsigned *bpm,unsigned *swing_x2);
#endif
