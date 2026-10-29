#ifndef BB_BROWSER_H
#define BB_BROWSER_H
#include "bb_catalogue.h"
#define BB_BROWSER_BANK_KEYS 16u
#define BB_BROWSER_PART_KEYS 8u
/* 1.5: ACID is Dillo-only (bass phrases with no drums) and optional: a catalogue without "acid" has no ACID banks. */
enum { BB_BROWSER_ERAS, BB_BROWSER_SONGS, BB_BROWSER_ACID, BB_BROWSER_GROUPS };
typedef struct {
    const bb_catalogue *catalogue; /* identity token; never a serialized pointer */
    bb_catalogue_id bank, part;
    uint16_t bank_index, part_index;
    uint8_t group;
} bb_factory_choice;
/* Control/UI-owned view over validated, immutable catalogue bytes that outlive
 * the view and its selections. Selecting
 * a cell changes the browser cursor only: it is NOT evidence of audio launch.
 * Playing/queued labels must come from the actual audio owner, not this view. */
typedef struct {
    const bb_catalogue *catalogue;
    uint16_t bank_index, part_index, bank_page, part_page;
    uint8_t group, ready;
} bb_browser;
int bb_browser_init(bb_browser *, const bb_catalogue *);
size_t bb_browser_bank_count(const bb_browser *);
size_t bb_browser_part_count(const bb_browser *);
size_t bb_browser_bank_pages(const bb_browser *);
size_t bb_browser_part_pages(const bb_browser *);
int bb_browser_group(bb_browser *, unsigned);
/* Signed movement wraps across actual entries/pages, never padded keys. */
int bb_browser_move_bank(bb_browser *, int);
int bb_browser_move_bank_page(bb_browser *, int);
int bb_browser_move_part_page(bb_browser *, int);
/* Lower sixteen keys select a bank; upper eight select a source section.
 * Bank selection uses Studio's actual initial section (not always index 0).
 * Invalid/empty cells leave the browser and optional output unchanged. */
int bb_browser_bank_key(bb_browser *, unsigned, bb_factory_choice *);
int bb_browser_part_key(bb_browser *, unsigned, bb_factory_choice *);
int bb_browser_current(const bb_browser *, bb_factory_choice *);
bb_catalogue_id bb_browser_bank_cell(const bb_browser *, unsigned);
bb_catalogue_id bb_browser_part_cell(const bb_browser *, unsigned);
int bb_browser_bank_title(const bb_browser *, unsigned, char *, size_t);
int bb_browser_part_title(const bb_browser *, unsigned, char *, size_t);
/* Re-resolve index+IDs against the immutable source, rejecting forged/stale
 * selections. Useful before preparing a factory pattern or complete scene. */
int bb_factory_choice_valid(const bb_catalogue *, const bb_factory_choice *);
#endif
