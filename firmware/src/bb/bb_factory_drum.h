#ifndef BB_FACTORY_DRUM_H
#define BB_FACTORY_DRUM_H
#include "bb_browser.h"
#include "bb_core.h"
#include "bb_pattern.h"
#define BB_FACTORY_DRUM_EVENTS 128u
enum {BB_FDV_BD,BB_FDV_SD,BB_FDV_LT,BB_FDV_MT,BB_FDV_HT,
      BB_FDV_LC,BB_FDV_MC,BB_FDV_HC,BB_FDV_RS,BB_FDV_C,
      BB_FDV_CP,BB_FDV_MA,BB_FDV_CB,BB_FDV_CY,BB_FDV_OHH,BB_FDV_HH,
      BB_FDV_RD,BB_FDV_SY,BB_FDV_ZAP,BB_FDV_ACCENT,BB_FDV_ACC,BB_FDV_COUNT};
enum {BB_FDL_SIXTEENTHS,BB_FDL_EIGHTHS,BB_FDL_TRIPLETS};
enum {BB_FDI_ABSENT,BB_FDI_ACCENTED,BB_FDI_UNACCENTED};
enum {BB_FDM_FLAM=1u,BB_FDM_RETRIGGER_RATE=2u,BB_FDM_RETRIGGER_LENGTH=4u};
enum {BB_FDRUM_OK,BB_FDRUM_INVALID,BB_FDRUM_LENGTH,BB_FDRUM_FIELD,
      BB_FDRUM_VOICE,BB_FDRUM_EXPRESSION,BB_FDRUM_TIMING,BB_FDRUM_MODE,
      BB_FDRUM_BUSY,BB_FDRUM_UNAVAILABLE};
typedef struct {
    bb_catalogue_id source; /* exact original event, including provenance */
    uint8_t voice,step,intensity,modifiers; /* source step is one-based */
} bb_factory_drum_event;
typedef struct {
    const bb_catalogue *catalogue;
    bb_catalogue_id source; /* original complete pattern and metadata */
    bb_factory_drum_event events[BB_FACTORY_DRUM_EVENTS];
    uint8_t length,lattice,count,bars; /* bars=0 means source did not declare it */
} bb_factory_drum_data;
typedef bb_drum_pattern_data bb_factory_drum_grid;
enum {BB_FDRUM_ISSUE_INVALID=1u,BB_FDRUM_ISSUE_TIMING=2u,
      BB_FDRUM_ISSUE_VOICE=4u,BB_FDRUM_ISSUE_MODIFIER=8u,
      BB_FDRUM_ISSUE_DUPLICATE=16u,BB_FDRUM_ISSUE_LANE=32u};
const char *bb_factory_drum_voice_name(unsigned voice);
/* Main/control thread only. Source fields are retained; no catalogue work
 * belongs in the audio interrupt. Every failure leaves the output unchanged.
 * Output is caller-owned and must not overlap the catalogue or its view. */
int bb_factory_read_drum(const bb_catalogue *,const bb_factory_choice *,bb_factory_drum_data *);
/* Internal preparation scratch only: failure may modify scratch. Never pass
 * live state or a published output. Same validation and result codes as read;
 * callers commit only after this and subsequent preparation succeed. */
int bb_factory_read_drum_scratch(const bb_catalogue *,const bb_factory_choice *,bb_factory_drum_data *);
/* Explicit projection to the current 808 step model. Returns issue bits;
 * unsupported projection never silently drops hits, voices or modifiers.
 * Eighth-note steps map to every second native sixteenth. Triplets require
 * richer timing. Per-event accent and source global accent both affect hits.
 * Alternate voices require compatible kit modes, not hidden sound changes. */
unsigned bb_factory_project_drum(const bb_factory_drum_data *,bb_factory_drum_grid *);
/* Caller-owned staging only. Preserve kit/sound, mix, clock, synths, UI and
 * transport. Copy steps/length and reset only this layer's sequence cursor.
 * Required lane modes must already match the selected kit. Not a live API. */
int bb_factory_stage_drum(const bb_catalogue *,const bb_factory_choice *,unsigned machine,bb_core *staging);
#endif
