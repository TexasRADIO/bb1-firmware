#include "bb_sound_picker.h"
#include <assert.h>
#include <string.h>

const bb_named_dillo bb_dillo_bank[BB_DILLO_PATCHES] = {0};
const char *const bb_drum_kit_names[BB_DRUM_KITS] = {0};

static bb_sample_status observed;
int bb_audio_sample_status_layer(bb_audio *a, unsigned layer, bb_sample_status *out) {
    (void)a;
    assert(layer == 0);
    *out = observed;
    return 1;
}
bb_sample_audio_binding *bb_audio_sample_layer(const bb_audio *a, unsigned layer) {
    (void)a; (void)layer;
    return 0;
}

int main(void) {
    bb_audio audio = {0};
    bb_sound_picker picker = {0};
    picker.ready = 1;
    picker.page = BB_DRUMS;
    picker.machine = 0;
    picker.sample_count = 1;
    picker.cursor = 1;
    picker.requested = 1;
    picker.epoch_before = 10;
    picker.pending = 1;
    picker.status = BB_SP_REQUESTED;
    memset(&observed, 0, sizeof observed);
    observed.count = 1;
    observed.serial = 10;
    observed.queue_idle = 0;
    assert(bb_sound_picker_poll_view(&picker, &audio, 0, 0));
    assert(picker.pending && picker.status == BB_SP_REQUESTED);
    observed.queue_idle = 1;
    assert(bb_sound_picker_poll_view(&picker, &audio, 0, 0));
    assert(!picker.pending && picker.status == BB_SP_FAILED);
    picker.pending = 1;
    picker.status = BB_SP_REQUESTED;
    observed.serial = 11;
    observed.requested = 0;
    observed.selected = 0;
    observed.result = 1;
    assert(bb_sound_picker_poll_view(&picker, &audio, 0, 0));
    assert(!picker.pending && picker.status == BB_SP_APPLIED);
    return 0;
}
