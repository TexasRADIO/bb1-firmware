/* Host regression: a chosen drum kit (NEXT KIT) ends exactly when the request carrying it ends.
 * Cases from #5/#31: chosen -> queued -> applied, ordinary refusal, STOP cancel, audio-fault refusal,
 * a stale/other ticket, a different kit, and a later phrase-only choice, on both drum layers. */
#include <stdio.h>
#include <string.h>
#include "bb_kit_intent.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); fails++; } } while (0)
static bb_player req(unsigned layer, uint32_t ticket, int has_kit, unsigned kit, uint32_t seen, uint8_t result) {
    bb_player p; memset(&p, 0, sizeof p);
    p.is_drum = 1; p.target = (uint8_t)layer; p.request.ticket = ticket; p.request.has_kit = (uint8_t)has_kit;
    p.request.kit = (uint8_t)kit; p.status.seen = seen; p.status.result = result; return p;
}
int main(void) {
    for (unsigned layer = 0; layer < 2; layer++) {
        bb_player p;
        p = req(layer, 7, 1, 1, 0, BB_PATTERN_NONE);     CHECK(!bb_kit_intent_settled(&p, 1, 1), "chosen: kept");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_QUEUED);   CHECK(!bb_kit_intent_settled(&p, 1, 1), "queued: kept");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_APPLIED);  CHECK(bb_kit_intent_settled(&p, 1, 1), "applied: cleared");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_REJECTED); p.error = 3; CHECK(bb_kit_intent_settled(&p, 1, 1), "refused: cleared");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_CANCELLED);CHECK(bb_kit_intent_settled(&p, 1, 1), "STOP cancel: cleared");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_REJECTED); CHECK(bb_kit_intent_settled(&p, 1, 1), "audio fault: cleared");
        p = req(layer, 7, 1, 1, 6, BB_PATTERN_APPLIED);  CHECK(!bb_kit_intent_settled(&p, 1, 1), "older ticket: kept");
        p = req(layer, 7, 1, 2, 7, BB_PATTERN_APPLIED);  CHECK(!bb_kit_intent_settled(&p, 1, 1), "other kit: kept");
        p = req(layer, 8, 0, 0, 8, BB_PATTERN_APPLIED);  CHECK(!bb_kit_intent_settled(&p, 1, 1), "phrase-only request: kept");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_APPLIED);  CHECK(!bb_kit_intent_settled(&p, 0, 1), "no intent: nothing to clear");
        p = req(layer, 7, 1, 1, 7, BB_PATTERN_APPLIED);  p.is_drum = 0; CHECK(!bb_kit_intent_settled(&p, 1, 1), "synth player: ignored");
    }
    CHECK(!bb_kit_intent_settled(NULL, 1, 1), "null player");
    printf(fails ? "kit intent: %d FAILED\n" : "kit intent: 11 cases x 2 layers + null ok (%d failures)\n", fails);
    return fails != 0;
}
