/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MASTER_SOUND_VIEW_H
#define BB_MASTER_SOUND_VIEW_H
#include "control.h"
#include "bb_sound_picker.h"
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
#include "studio_rows.h"
#endif
typedef struct {
    const char *name; /* static factory name; never a temporary cursor buffer */
    int index;
    uint8_t ready, edited, enabled, muted;
} hm_sound_row;
static inline int hm_sound_rows_with_catalogue(bb_audio *audio, unsigned layer,
                                hm_sound_row rows[HM_INSTRUMENTS],
                                const char *const *names,unsigned count) {
    if (!audio || !rows || layer >= BB_MACHINES) return 0;
#if !defined(BB_TWO_DRUM_HOST_EXPERIMENT)
    static const bb_page pages[HM_INSTRUMENTS] = {BB_DRUMS,BB_DILLO,BB_TELEGRAM};
#endif
    hm_sound_row next[HM_INSTRUMENTS] = {0};
    for (unsigned i = 0; i < HM_INSTRUMENTS; ++i) {
        /* Read-only observer: no launch, persistent picker, or extra DSP state.
         * Copy under the existing guard, match factory names outside it. */
        bb_sound_picker observer = {0};
        bb_sound_picker_view applied;
        next[i].index = -1;
        next[i].name = "UNAVAILABLE";
#if defined(BB_TWO_DRUM_HOST_EXPERIMENT)
        if(!st_sound_open(i,&observer,audio,names,count))continue;
#else
        if(!bb_sound_picker_open(&observer,audio,pages[i],i==HM_DRUMS?layer:0,names,count))continue;
#endif
        if (!bb_sound_picker_observe(&observer, audio, &applied)) continue;
        next[i].ready = 1;
        next[i].index = applied.active_index;
        next[i].edited = applied.edited;
        next[i].enabled = applied.enabled;
        next[i].muted = applied.muted;
        next[i].name = applied.active_index < 0 ? "CUSTOM" :
            bb_sound_picker_item_name(&observer, (unsigned)applied.active_index);
        if (!next[i].name) next[i].name = "UNKNOWN";
    }
    for (unsigned i = 0; i < HM_INSTRUMENTS; ++i) rows[i] = next[i];
    return 1;
}
static inline int hm_sound_rows(bb_audio *audio,unsigned layer,
                                hm_sound_row rows[HM_INSTRUMENTS]) {
    return hm_sound_rows_with_catalogue(audio,layer,rows,NULL,0);
}
#endif
