/* Host regression (issue #39 request): every terminal outcome of a sampled-kit request on BOTH drum
 * layers, through the real bb_sound_picker_poll_view(): still queued -> pending; applied; refused by
 * the audio owner; superseded by another request; admission dropped (queue drained, no serial
 * advance). Note: "queue idle proves completion" holds while the command consumer runs on CPU0
 * inside the audio callback (target model); a consumer on another thread would invalidate it. */
#include "bb_sound_picker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
const bb_named_dillo bb_dillo_bank[BB_DILLO_PATCHES] = {0};
const char *const bb_drum_kit_names[BB_DRUM_KITS] = {0};
static bb_sample_status observed[2];
int bb_audio_sample_status_layer(bb_audio *a, unsigned layer, bb_sample_status *out) {
    (void)a; assert(layer < 2); *out = observed[layer]; return 1;
}
bb_sample_audio_binding *bb_audio_sample_layer(const bb_audio *a, unsigned layer) { (void)a; (void)layer; return 0; }
static bb_sound_picker request(unsigned layer, unsigned kit) {
    bb_sound_picker p; memset(&p, 0, sizeof p);
    p.ready = 1; p.page = BB_DRUMS; p.machine = (uint8_t)layer; p.sample_count = 3; p.cursor = kit;
    p.requested = kit; p.epoch_before = 10; p.pending = 1; p.status = BB_SP_REQUESTED; return p;
}
int main(void) {
    bb_audio audio = {0}; unsigned cases = 0;
    for (unsigned layer = 0; layer < 2; layer++) {
        bb_sound_picker p;
        memset(observed, 0, sizeof observed);
        observed[layer].count = 3; observed[layer].serial = 10; observed[layer].queue_idle = 0;
        p = request(layer, 1);                                                   /* queued */
        assert(bb_sound_picker_poll_view(&p, &audio, 0, 0) && p.pending && p.status == BB_SP_REQUESTED); cases++;
        /* picker item k = audio sample kit k-1 (item 0 is the native 808) */
        observed[layer].serial = 11; observed[layer].requested = 0; observed[layer].selected = 0; observed[layer].result = 1;
        assert(bb_sound_picker_poll_view(&p, &audio, 0, 0) && !p.pending && p.status == BB_SP_APPLIED); cases++;  /* applied */
        p = request(layer, 2); observed[layer].serial = 11; p.epoch_before = 10; observed[layer].requested = 1; observed[layer].result = 2;                         /* refused */
        assert(bb_sound_picker_poll_view(&p, &audio, 0, 0) && !p.pending && p.status == BB_SP_FAILED); cases++;
        p = request(layer, 2); observed[layer].serial = 12; observed[layer].requested = 1; observed[layer].result = 1;  /* superseded */
        assert(bb_sound_picker_poll_view(&p, &audio, 0, 0) && !p.pending && p.status == BB_SP_SUPERSEDED); cases++;
        p = request(layer, 1); observed[layer].serial = 10; observed[layer].queue_idle = 1;                        /* dropped */
        assert(bb_sound_picker_poll_view(&p, &audio, 0, 0) && !p.pending && p.status == BB_SP_FAILED); cases++;
    }
    printf("sound picker outcomes: %u cases on 2 layers ok\n", cases);
    return 0;
}
