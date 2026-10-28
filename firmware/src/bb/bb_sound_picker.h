/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_SOUND_PICKER_H
#define BB_SOUND_PICKER_H
#include "bb_audio.h"

/* Main-loop controller only. This does not introduce another sound engine,
 * a target input binding, a callback on the audio thread, or a save format. */
enum {BB_SP_IDLE,BB_SP_REQUESTED,BB_SP_APPLIED,BB_SP_SAME,BB_SP_FAILED,BB_SP_SUPERSEDED};
enum {BB_SP_OK,BB_SP_BUSY,BB_SP_INVALID,BB_SP_UNCHANGED,BB_SP_REFUSED};
typedef struct {
    uint32_t epoch_before;
    uint8_t page,machine,cursor,requested,ready,pending,status;
#if BB_SAMPLE_HOST_INTEGRATION
    uint8_t sample_count;
    const char *const *sample_names; /* immutable UI labels, same registered ID order */
#endif
} bb_sound_picker;
typedef struct {
    int active_index; /* -1 = actual synth values do not match a factory patch */
    uint32_t family_epoch;
    uint8_t edited,enabled,muted,running,queue_idle,stop_queued;
#if BB_SAMPLE_HOST_INTEGRATION
    uint16_t sample_compatible; /* hybrid list needs nine bits:808 + eight samples */
    uint8_t sample_result,sample_requested;
#endif
} bb_sound_picker_view;
typedef struct {
    void *context;
    int (*request)(void *,unsigned layer,unsigned family,unsigned recipe);
    int (*busy)(void *);
} bb_sound_picker_kits;

unsigned bb_sound_picker_count(bb_page);
const char *bb_sound_picker_name(bb_page,unsigned index);
unsigned bb_sound_picker_items(const bb_sound_picker *);
const char *bb_sound_picker_item_name(const bb_sound_picker *,unsigned);
int bb_sound_picker_available(const bb_sound_picker *,const bb_sound_picker_view *,unsigned);
#if BB_SAMPLE_HOST_INTEGRATION
int bb_sound_picker_sample_init(bb_sound_picker *,bb_audio *,const char *const *,unsigned);
#endif
int bb_sound_picker_init(bb_sound_picker *,bb_audio *,bb_page,unsigned layer);
/* Main-loop entry routing. Sample labels must match the cold-registered audio
 * catalogue; absent metadata must never fall back to misleading 808 names. */
int bb_sound_picker_open(bb_sound_picker *,bb_audio *,bb_page,unsigned layer,
                         const char *const *sample_names,unsigned sample_count);
int bb_sound_picker_move(bb_sound_picker *,int detents);
int bb_sound_picker_key(bb_sound_picker *,unsigned key);
/* Snapshot only the relevant applied sound, under the existing queue guard.
 * Matching names and formatting occur after the guard is released. */
int bb_sound_picker_observe(const bb_sound_picker *,bb_audio *,bb_sound_picker_view *);
/* An accepted request is not an applied sound. Kit requests reuse the caller's
 * existing guarded transaction. Synth requests carry an explicit target and
 * use the retained command queue; they do not alter the UI's patch labels. */
int bb_sound_picker_commit(bb_sound_picker *,bb_audio *,const bb_sound_picker_kits *);
int bb_sound_picker_poll(bb_sound_picker *,bb_audio *,const bb_sound_picker_kits *);
/* Returns the same applied snapshot used to resolve the request. The view is
 * published only on success; callers need not observe the audio state twice. */
int bb_sound_picker_poll_view(bb_sound_picker *,bb_audio *,const bb_sound_picker_kits *,bb_sound_picker_view *);
#endif
