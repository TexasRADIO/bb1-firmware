#ifndef BB_PLAYER_H
#define BB_PLAYER_H
#include "bb_factory_launch.h"
#include "bb_factory_drum_launch.h"
#ifndef BB_WITH_RICH_DRUM_BROWSER
#define BB_WITH_RICH_DRUM_BROWSER 0
#endif
#if BB_WITH_RICH_DRUM_BROWSER && !BB_WITH_RICH_DRUM_RENDER
#error "Rich drum browser requires copied rich playback"
#endif
enum { BB_PLAYER_NEEDS_KIT=BB_FACTORY_PATTERN_UNAVAILABLE+1,
       BB_PLAYER_NEEDS_TIMING,BB_PLAYER_NEEDS_VOICE };
/* Reliability r1 (D4): terminal, never-applied outcomes of an owned request.
 * Stable values; display text is bb_notice_text() in bb_fault_notice.h. */
enum { BB_PLAYER_CANCELLED=BB_PLAYER_NEEDS_VOICE+1 /* = 10, CANCELLED BY STOP */,
       BB_PLAYER_AUDIO_FAULT /* = 11, AUDIO FAULT: REQUEST ENDED (D1) */ };
typedef struct {
    bb_factory_choice choice;
    uint32_t ticket;
    uint8_t kit,has_kit; /* Only a committed pair carries a kit identity. */
} bb_player_selection;
/* Main-loop owned. A cursor is not a playing label. The immutable catalogue
 * and audio object must outlive this controller; reset after audio restore. */
typedef struct {
    bb_browser view;
    bb_player_selection request,playing;
    bb_pattern_status status;
    uint8_t target,is_drum,ready,error,request_seen;
#if BB_WITH_RICH_DRUM_BROWSER
    uint8_t request_rich;
#endif
} bb_player;
int bb_player_init(bb_player *,const bb_browser *validated_view,unsigned synth);
int bb_player_init_drum(bb_player *,const bb_browser *validated_view,unsigned machine);
int bb_player_sync(bb_player *,bb_audio *);
int bb_player_bank_key(bb_player *,bb_audio *,unsigned key);
int bb_player_part_key(bb_player *,bb_audio *,unsigned key);
int bb_player_move(bb_player *,bb_audio *,int delta);
/* Cursor-only family/page navigation never launches music. */
int bb_player_group(bb_player *,unsigned group);
int bb_player_page(bb_player *,int delta,int sections);
/* 0=no owned pending request, 1=accepted/not yet consumed, 2=audio queued. */
unsigned bb_player_pending(const bb_player *);
#endif
