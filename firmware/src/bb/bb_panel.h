#ifndef BB_PANEL_H
#define BB_PANEL_H
#include "bb_control.h"
/* Main-loop-owned requested state; no unsynchronized reads of audio patterns.
 * Selection and bank are UI-only. Music changes become visible here ONLY after
 * the queue accepts them. Audio consumes the same command at its next block. */
typedef struct {
    bb_core requested;
    uint8_t machine,voice,bank,accent,animate;
    uint32_t rejected;
} bb_panel;
void bb_panel_init(bb_panel *,const bb_core *stopped_boot_state);
/* Same-core, IRQ-protected pattern/length refresh. Copy only when the command
 * FIFO is empty, preserving optimistic pending edits and UI-only selection.
 * Returns 0 if busy/invalid, 1 unchanged, 2 refreshed with differences.
 * Target must supply the usual bounded IRQ hooks; NULL is host-only. */
int bb_panel_sync_patterns(bb_panel *,bb_commands *,const bb_core *);
/* Mask bits: drum machines first, then synths. Same guarded snapshot contract. */
int bb_panel_sync_patterns_mask(bb_panel *,bb_commands *,const bb_core *,unsigned);
int bb_panel_send(bb_panel *,bb_commands *,const bb_command *);
int bb_panel_step(bb_panel *,bb_commands *,unsigned step_in_bank);
/* Audition a named voice on the selected part, independently of step editing.
 * Muted/disabled sequencers may be auditioned; their mixer levels still apply. */
int bb_panel_drum_hit(bb_panel *,bb_commands *,unsigned voice,unsigned velocity);
int bb_panel_synth_step(bb_panel *,bb_commands *,unsigned synth,unsigned step_in_bank,bb_edit edit,unsigned pitch);
int bb_panel_synth_pitch(bb_panel *,bb_commands *,unsigned synth,unsigned absolute_step,unsigned pitch);
int bb_panel_synth_shift(bb_panel *,bb_commands *,unsigned synth,unsigned absolute_step,int delta);
/* Physical note keys 0..26 from F: sixteen white steps, eleven black helpers. */
int bb_panel_white_step(unsigned key);
int bb_panel_black_index(unsigned key);
#endif
