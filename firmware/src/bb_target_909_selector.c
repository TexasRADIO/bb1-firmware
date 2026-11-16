/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_switch.h"
#define bb_kit_family (bb_sound.drum_patterns.family)
static const char *bb_kit_switch_message;
static int bb_kit_switch_busy(void){return 0;}
static int bb_kit_switch_poll(void){return 0;}
int bb_kit_switch_request(unsigned layer,unsigned family,unsigned recipe) {
    if(family||recipe>=BB_DRUM_KITS)return 0;
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    if(layer>=2)return 0;
#else
    if(layer)return 0;
#endif
    bb_command c={0};c.type=BB_CMD_DRUM_KIT;c.target=(uint8_t)layer;c.value=(uint16_t)recipe;
    int ok=bb_command_push(&bb_sound.commands,&c);
    bb_kit_switch_message=ok?"SOUND QUEUED":"BUSY / RETRY";return ok;
}
