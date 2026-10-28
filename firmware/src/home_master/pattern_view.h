/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_MASTER_PATTERN_VIEW_H
#define BB_MASTER_PATTERN_VIEW_H
#include "pattern_route.h"
typedef struct {
    bb_player_selection playing, requested;
    unsigned pending;
    uint8_t ready, focused, error;
} hm_pattern_row;
/* Main-loop snapshot for the three-row screen. No catalogue expansion,
 * launch, transport change or guessed playing label. Blank identity remains
 * blank when a coordinated bank or another editor owns the current phrase. */
static inline int hm_pattern_rows(const hm_state *focus, bb_player *drums,
                                   bb_player *synths, unsigned layer,
                                   bb_audio *audio, hm_pattern_row rows[HM_INSTRUMENTS]) {
    if (!focus || !audio || !rows || focus->selected >= HM_INSTRUMENTS)
        return 0;
    hm_pattern_row next[HM_INSTRUMENTS] = {0};
    for (unsigned i = 0; i < HM_INSTRUMENTS; ++i) {
        hm_state selected = {(uint8_t)i};
        bb_player *p = hm_ready_player(&selected, drums, synths, layer);
        next[i].focused = i == focus->selected;
        if (!p || !bb_player_sync(p, audio)) continue;
        next[i].ready = 1;
        next[i].playing = p->playing;
        next[i].pending = bb_player_pending(p);
        if (next[i].pending) next[i].requested = p->request;
        next[i].error = p->error;
    }
    for (unsigned i = 0; i < HM_INSTRUMENTS; ++i) rows[i] = next[i];
    return 1;
}
#endif
