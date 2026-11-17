/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_PLAYER_PREVIEW_H
#define BB_PLAYER_PREVIEW_H
#include "bb_player.h"
/* Main-loop, single-producer operations. No allocation or audio ownership.
 * Preview functions return 1/0 and only change the catalogue cursor.
 * They do not change request, playing, error, status or transport.
 * A zero-detent preview is a no-op, including the selected section.
 */
int bb_player_preview_move(bb_player *,int delta);
int bb_player_preview_bank_key(bb_player *,unsigned key);
int bb_player_preview_part_key(bb_player *,unsigned key);
#if BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
/* Main-loop kit-aware preview. No command queue, audio or playing-state edits.
 * A kit change aligns the cursor to a playable section; incompatible key
 * cells refuse without moving it. The commit path still revalidates. */
int bb_player_preview_kit_playable(const bb_player *,const bb_audio *,unsigned kit);
/* Control thread only. Preview, not an applied/playing claim. Output changes
 * only on success; counts distinct source roles, not individual hits. */
int bb_player_preview_substitutions(const bb_player *,const bb_audio *,unsigned kit,
                                    unsigned *count,unsigned *first,unsigned *replacement);
int bb_player_preview_align_kit(bb_player *,const bb_audio *,unsigned kit);
int bb_player_preview_move_kit(bb_player *,const bb_audio *,unsigned kit,int delta);
int bb_player_preview_bank_key_kit(bb_player *,const bb_audio *,unsigned kit,unsigned key);
int bb_player_preview_part_key_kit(bb_player *,const bb_audio *,unsigned kit,unsigned key);
int bb_player_preview_group_kit(bb_player *,const bb_audio *,unsigned kit,unsigned group);
#endif
/* Same result codes and checked submission path as the existing live API.
 * Confirms the exact highlighted section, not the bank's initial section.
 * Accepted is not playing: keep using the existing audio acknowledgements.
 * Existing bb_player_move/bank_key/part_key remain turn-to-audition APIs.
 */
int bb_player_confirm(bb_player *,bb_audio *);
/* Host/target trial: validate and submit the highlighted kit and phrase as
 * one audio-boundary transaction. Accepted does not mean applied. */
int bb_player_confirm_with_kit(bb_player *,bb_audio *,unsigned kit);
#endif
