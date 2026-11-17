/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop adapter for the two-drum + Dillo profile. Not an audio owner.
 * Physical row, drum target, synth target and mixer bus are distinct domains.
 */
#ifndef BB_STUDIO_ROWS_H
#define BB_STUDIO_ROWS_H
#include "bb_player.h"
#include "bb_sound_picker.h"
#if BB_WITH_TELEGRAM
enum { ST_DRUM_A, ST_DRUM_B, ST_DILLO, ST_LEAD, ST_ROWS };   /* Dillo Duo: BASS, then LEAD */
#else
enum { ST_DRUM_A, ST_DRUM_B, ST_DILLO, ST_ROWS };
#endif
typedef struct { bb_page page; unsigned target; const char *label; } st_route;
static inline int st_resolve(unsigned row, st_route *out) {
    static const st_route routes[ST_ROWS] = {
        {BB_DRUMS,0,"BIG BEAT A"}, {BB_DRUMS,1,"BIG BEAT B"},
        {BB_DILLO,0,BB_WITH_TELEGRAM?"DILLO BASS":"DILLO"},
#if BB_WITH_TELEGRAM
        {BB_TELEGRAM,1,"DILLO LEAD"},
#endif
    };
    if (!out || row >= ST_ROWS) return 0;
    *out=routes[row]; return 1;
}
/* SELECT changes focus only, never emits a sound/pattern/transport command. */
static inline int st_select(unsigned *row,int delta) {
    if (!row || *row>=ST_ROWS) return 0;
    int step=delta>127?127:delta< -127?-127:delta;
    int next=(int)*row+step;
    *row=(unsigned)(next<0?0:next>=ST_ROWS?ST_ROWS-1:next);
    return 1;
}
/* HOME preserves the editor's actual target. Unsupported pages/layers do
 * not silently become drum A or a non-existent Telegram row. */
static inline int st_from_editor(bb_page page,unsigned target,unsigned *row) {
    if (!row) return 0;
    if (page==BB_DRUMS && target<2) { *row=target;return 1; }
    if (page==BB_DILLO && target==0) { *row=ST_DILLO;return 1; }
#if BB_WITH_TELEGRAM
    if (page==BB_TELEGRAM) { *row=ST_LEAD;return 1; }
#endif
    return 0;
}
/* Never substitute A when B's player is absent, unready or misbound. */
static inline bb_player *st_ready_player(unsigned row, bb_player *drums,
                                        bb_player *synths) {
    st_route r;
    if (!drums || !synths || !st_resolve(row,&r)) return 0;
    bb_player *p=r.page==BB_DRUMS ? &drums[r.target] : &synths[r.target];
    return p->ready && p->is_drum==(r.page==BB_DRUMS) &&
           p->target==r.target ? p : 0;
}
static inline int st_sound_open(unsigned row,bb_sound_picker *picker,
                               bb_audio *audio,const char *const *names,
                               unsigned count) {
    st_route r;
    if (!st_resolve(row,&r)) return 0;
    return bb_sound_picker_open(picker,audio,r.page,r.target,names,count);
}
/* Absolute values come from an acknowledged state snapshot. Caller owns
 * queue admission; constructing a command does not imply it was applied.
 * A/B levels precede their shared drum bus; Dillo has its own mixer bus.
 */
static inline int st_level(unsigned row,unsigned value,bb_command *out) {
    st_route r;
    if (!out || value>127 || !st_resolve(row,&r)) return 0;
    bb_command c={0};
    c.type=r.page==BB_DRUMS ? BB_CMD_DRUM_LEVEL : BB_CMD_BAND_LEVEL;
    c.target=(uint8_t)(r.page==BB_DRUMS ? r.target : r.page==BB_TELEGRAM ? BB_MIX_TELEGRAM : BB_MIX_DILLO);
    c.value=(uint16_t)value; *out=c; return 1;
}
static inline int st_mute(unsigned row,unsigned muted,bb_command *out) {
    st_route r;
    if (!out || muted>1 || !st_resolve(row,&r)) return 0;
    bb_command c={0};
    c.type=r.page==BB_DRUMS ? BB_CMD_DRUM_MUTE : BB_CMD_SYNTH_MUTE;
    c.target=(uint8_t)r.target; c.value=(uint16_t)muted;
    *out=c; return 1;
}
#endif
