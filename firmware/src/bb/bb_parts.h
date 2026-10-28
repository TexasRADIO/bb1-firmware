#ifndef BB_PARTS_H
#define BB_PARTS_H
#include "bb_core.h"
#include "bb_trio.h"

#define BB_PART_COUNT 8u
#define BB_PART_NONE 255u
enum { BB_PART_REJECTED, BB_PART_QUEUED, BB_PART_APPLIED, BB_PART_UNCHANGED, BB_PART_CANCELLED };
/* Audio-consumer-owned RAM bank. Runtime fields in snapshots are normalized
 * away and never restored. No heap, storage, DSP reset or cross-core sharing.
 * The UI must use a command/ownership protocol, not call these concurrently.
 * PCM adapters must synchronize sounds/effects on BB_PART_APPLIED. */
typedef struct {
    bb_core scenes[BB_PART_COUNT];
    uint8_t phrase_steps[BB_PART_COUNT];
    uint8_t active, queued, position, ready;
    uint32_t launches;
    uint8_t family[BB_PART_COUNT][BB_MACHINES],current_family[BB_MACHINES];
} bb_parts;
/* Initial eight copies of the current setup. Does not invent variations.
 * All APIs require core/source storage distinct from the bank itself. */
int bb_parts_init(bb_parts *, const bb_core *, unsigned phrase_steps);
/* Exclusive owner only. Tags identify the lane model, not a DSP owner or patch.
 * Mixed-family recall must go through the suspended engine coordinator; these
 * real-time APIs reject it rather than reinterpreting saved lane bits. */
int bb_parts_init_families(bb_parts *,const bb_core *,unsigned,const uint8_t [BB_MACHINES]);
int bb_parts_bind_families(bb_parts *,const uint8_t [BB_MACHINES]);
/* Prepare an inactive slot while rendering/producers are suspended, or on
 * the audio consumer. Active edits are retained on departure. */
int bb_parts_store(bb_parts *, unsigned slot, const bb_core *, unsigned phrase_steps);
/* Audio owner only. Fully validate before replacing bank/current music.
 * Explicit bank replacement discards the old bank, never invents variations.
 * Preserves live clock/transport/UI and publishes the requested initial part.
 * Source must be distinct from both objects; no decoding or allocation here. */
int bb_parts_load_trio(bb_parts *,bb_core *,const bb_trio_program *);
/* Stopped: apply immediately. Playing: last request wins at the next phrase.
 * Selecting the active part cancels a pending launch without reloading it. */
int bb_parts_request(bb_parts *, bb_core *, unsigned slot);
/* Invoke exactly once before every sequencer tick; returns APPLIED only for
 * an actual transition. The next part's first events follow this call. */
int bb_parts_before_tick(bb_parts *, bb_core *);
/* Consume the shared transport command in order. Stop cancels a pending
 * launch; stop/start both reset phrase position without discarding edits. */
void bb_parts_transport(bb_parts *);
#endif
