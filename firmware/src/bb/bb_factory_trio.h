/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_FACTORY_TRIO_H
#define BB_FACTORY_TRIO_H
#include "bb_factory_drum.h"
#include "bb_factory_pattern.h"
#include "bb_factory_recipe.h"
#include "bb_trio.h"
enum {BB_FTRIO_OK,BB_FTRIO_INVALID,BB_FTRIO_RECIPE,BB_FTRIO_LAYOUT,
      BB_FTRIO_CONTENT,BB_FTRIO_GREYHOUND,BB_FTRIO_SWING,BB_FTRIO_FAMILY};
enum {BB_FTRIO_THREE_ENGINE=1u}; /* explicit labelled interpretation, not full Studio */
typedef struct {
    bb_catalogue_id source;
    uint16_t drum_issues,mode_mismatch;
    uint8_t drum_result,synth_result[BB_SYNTHS],phrase;
} bb_factory_trio_part_report;
typedef struct {
    bb_factory_trio_part_report part[BB_TRIO_PARTS];
    uint8_t capabilities,recipe_result,initial,primary;
} bb_factory_trio_report;
typedef struct {
    bb_factory_recipe recipe;
    union {
        struct {bb_factory_drum_data data;bb_factory_drum_grid grid;} drum;
        bb_pattern_data synth;
    } parsed;
} bb_factory_trio_workspace;
/* Main/control producer only. Catalogue and inherited coherent snapshot must
 * remain immutable through the call. No live core/IRQ parsing or allocation.
 * Caller owns out exclusively (e.g. WRITING trio lease); failure means ABORT,
 * never publish: out is disposable and may be partially written. Workspace is
 * caller-owned scratch. Report is returned after valid selection/inheritance,
 * even if some sections reject; invalid arguments/selection leave it unchanged.
 * Invalid aliases leave everything unchanged.
 * Checks all eight A-H sections before writing a compact program. Kit/modes,
 * other drums, FX/master inherit the snapshot; no hidden kit changes/enabling.
 * Source Dillo/Telegram sounds, levels, enable flags and exact tempo/swing load.
 * Default refuses GreyHound; THREE_ENGINE explicitly accepts the labelled
 * interpretation and still reports missing GreyHound. Does not launch or save.
 * All input/output/work/report regions must be pairwise disjoint and outside
 * immutable catalogue data. A selection's part index sets the initial part. */
int bb_factory_prepare_trio(const bb_catalogue *,const bb_factory_choice *,
    const bb_core *inherited,unsigned primary,unsigned policy,
    bb_trio_program *out,bb_factory_trio_workspace *,bb_factory_trio_report *);
#endif
