/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_editor_snapshot.h"
#include "bb_editor_protocol.h"
#include "bb_swing.h"

static int put8(bb_editor_snapshot *s, uint8_t value) {
    if (s->len >= BB_SNAPSHOT_MAX) return 0;
    s->buf[s->len++] = value;
    return 1;
}
static int put16(bb_editor_snapshot *s, uint16_t value) {
    return put8(s, (uint8_t)(value >> 8)) && put8(s, (uint8_t)value);
}
static int put32(bb_editor_snapshot *s, uint32_t value) {
    return put8(s, (uint8_t)(value >> 24)) &&
           put8(s, (uint8_t)(value >> 16)) &&
           put8(s, (uint8_t)(value >> 8)) && put8(s, (uint8_t)value);
}
static int section_begin(bb_editor_snapshot *s, uint8_t scope) {
    int offset;
    if (!put8(s, scope) || !put8(s, 0) || !put8(s, 0)) return -1;
    offset = (int)s->len - 2;
    return offset;
}
static void section_end(bb_editor_snapshot *s, int offset) {
    uint16_t length = (uint16_t)(s->len - (uint16_t)offset - 2u);
    s->buf[offset] = (uint8_t)(length >> 8);
    s->buf[offset + 1] = (uint8_t)length;
}

int bb_editor_snapshot_build(bb_editor_snapshot *s, const bb_core *core,
                             uint32_t revision) {
    int off;
    unsigned i;
    if (!s || !core) return 0;
    s->len = 0;
    s->revision = revision;
    if (!put32(s, revision)) return 0;

    off = section_begin(s, BB_SNAP_GLOBAL);
    if (off < 0 || !put16(s, (uint16_t)(core->bpm * 10u)) ||
        !put8(s, (uint8_t)bb_swing_x2(core->swing)) ||
        !put32(s, core->sample_rate)) return 0;
    section_end(s, off);

    off = section_begin(s, BB_SNAP_TRANSPORT);
    if (off < 0 || !put8(s, core->running) || !put8(s, core->pending_tick) ||
        !put32(s, core->ticks) || !put32(s, (uint32_t)core->frame) ||
        !put8(s, (uint8_t)core->page) || !put8(s, core->selected_machine) ||
        !put8(s, core->selected_voice)) return 0;
    section_end(s, off);

    for (i = 0; i < 2; ++i) {
        off = section_begin(s, i ? BB_SNAP_DRUM_B : BB_SNAP_DRUM_A);
        if (off < 0 || !put8(s, core->drums[i].muted) ||
            !put8(s, core->drums[i].level) ||
            !put8(s, core->drums[i].length)) return 0;
        section_end(s, off);
    }

    off = section_begin(s, BB_SNAP_DILLO);
    if (off < 0 || !put16(s, core->dillo.cutoff_hz) ||
        !put16(s, core->dillo.resonance_x100) ||
        !put8(s, core->dillo.wave) || !put8(s, core->dillo.envelope) ||
        !put8(s, core->dillo.decay) || !put8(s, core->dillo.accent) ||
        !put8(s, core->dillo.drive) || !put8(s, core->dillo.slide_ms) ||
        !put8(s, core->dillo.attack_ms) || !put8(s, core->dillo.tracking)) return 0;
    section_end(s, off);

    off = section_begin(s, BB_SNAP_FX);
    if (off < 0) return 0;
    for (i = 0; i < BB_FX_PARAMETER_COUNT; ++i)
        if (!put16(s, core->effects.value[i])) return 0;
    if (!put8(s, core->fx_ext.delay_type) ||
        !put8(s, core->fx_ext.delay_division) ||
        !put8(s, core->fx_ext.dist_type) ||
        !put8(s, core->fx_ext.comp_amount)) return 0;
    section_end(s, off);

    off = section_begin(s, BB_SNAP_MIX);
    if (off < 0) return 0;
    for (i = 0; i < BB_MIX_CHANNELS; ++i)
        if (!put8(s, core->band_level[i])) return 0;
    section_end(s, off);

    return (int)s->len;
}
