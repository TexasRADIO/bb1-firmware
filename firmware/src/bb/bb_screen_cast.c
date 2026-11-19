/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.3: Screen companion — compact UI state cast encoder.
 * Copyright (C) 2026 Big Beat Project */
#include "bb_screen_cast.h"
#include "../bb_ui_v3/bb_ui.h"
#include <string.h>

/* ── Raw payload packing ──────────────────────────────────────────── */

#define RAW_CAP BB_CAST_MAX_RAW

/* Bounded change-detection history. These arrays retain the static storage
 * layout used by this release. */
static uint8_t prev_raw[RAW_CAP];
static uint8_t prev_len;

static inline void p8(uint8_t *b, int *p, uint8_t v) { if (*p < RAW_CAP) b[(*p)++] = v; }
static inline void p16(uint8_t *b, int *p, uint16_t v) { p8(b, p, (uint8_t)v); p8(b, p, (uint8_t)(v >> 8)); }

static int pack_raw(const bb_ui_in *in, uint8_t *raw)
{
    int p = 0;

    /* core state (0-13) */
    p8(raw, &p, in->page);
    p8(raw, &p, in->instr);
    p8(raw, &p, in->running);
    p8(raw, &p, in->beat);
    p8(raw, &p, in->step);
    p16(raw, &p, in->bpm_x10);
    p8(raw, &p, in->accent);
    p8(raw, &p, in->queued);
    p8(raw, &p, in->fault);
    p8(raw, &p, in->level);
    p16(raw, &p, (uint16_t)(in->events & 0xFFFFu));
    p8(raw, &p, in->ev_knob);

    /* knobs (14-22) */
    for (int k = 0; k < 4; k++) {
        p8(raw, &p, in->knob[k].bar);
        p8(raw, &p, in->knob[k].flags);
    }
    p8(raw, &p, in->knob_turning);

    /* transport detail (23-26) */
    p8(raw, &p, in->bar);
    p8(raw, &p, in->bars);
    p8(raw, &p, in->fill);
    p8(raw, &p, in->fill_beats);

    /* groups (27-38) */
    for (int g = 0; g < 4; g++) p8(raw, &p, in->group_level[g]);
    for (int g = 0; g < 4; g++) p8(raw, &p, in->group_out[g]);
    p8(raw, &p, in->group_muted);
    p8(raw, &p, in->out_peak[0]);
    p8(raw, &p, in->out_peak[1]);
    p8(raw, &p, in->dsp);

    /* drum steps (39-46): 4 groups × uint16 LE */
    for (int g = 0; g < 4; g++) p16(raw, &p, in->gsteps[g]);

    /* Dillo note data (47-65): only when relevant */
    for (int n = 0; n < 16; n++) p8(raw, &p, (uint8_t)in->note[n]);
    p8(raw, &p, in->cutoff);
    p8(raw, &p, in->reso);
    p8(raw, &p, in->envmod);

    /* FX (66-79) */
    p16(raw, &p, in->echo_ms);
    p8(raw, &p, in->heads);
    p8(raw, &p, in->fx_delay_type);
    p8(raw, &p, in->fx_dist_type);
    p8(raw, &p, in->fx_drive);
    p8(raw, &p, in->fx_comp);
    p8(raw, &p, in->fx_comp_gr);
    p8(raw, &p, in->fx_rv_decay);
    p8(raw, &p, in->fx_rv_level);
    p8(raw, &p, in->fx_motion);
    p8(raw, &p, in->fx_wear);

    /* sections (80-83) */
    p8(raw, &p, in->sections);
    p8(raw, &p, in->sec_cur);
    p8(raw, &p, in->sec_next);
    p8(raw, &p, in->sec_pos);

    /* tabs (84-85) */
    p8(raw, &p, in->tabs);
    p8(raw, &p, in->tab_cur);

    /* tile page and voice for STEPS (86-87) */
    p8(raw, &p, in->tile_page);
    p8(raw, &p, in->voice);

    return p;
}

/* ── 7-bit SysEx encoding ─────────────────────────────────────────── */
/* Standard MIDI 8-to-7: for every 7 data bytes, one header byte carries
 * the MSBs, and the 7 data bytes have their MSBs cleared. */

static int encode_7bit(const uint8_t *raw, int raw_len, uint8_t *out, int maxout)
{
    int wp = 0, rp = 0;
    while (rp < raw_len) {
        int chunk = raw_len - rp;
        if (chunk > 7) chunk = 7;
        if (wp + 1 + chunk > maxout) break;
        uint8_t msb = 0;
        for (int i = 0; i < chunk; i++)
            msb |= (uint8_t)(((raw[rp + i] >> 7) & 1u) << i);
        out[wp++] = msb;
        for (int i = 0; i < chunk; i++)
            out[wp++] = raw[rp + i] & 0x7Fu;
        rp += chunk;
    }
    return wp;
}

/* ── Public API ────────────────────────────────────────────────────── */

int bb_cast_encode(const void *in_ptr, uint8_t *buf, int maxlen)
{
    const bb_ui_in *in = (const bb_ui_in *)in_ptr;
    uint8_t raw[RAW_CAP];
    int raw_len = pack_raw(in, raw);

    /* Change detection: skip if identical to previous send */
    if (raw_len == prev_len &&
        memcmp(raw, prev_raw, (unsigned)raw_len) == 0)
        return 0;

    /* SysEx header: F0 00 22 24 35 02 */
    int hdr = 6;
    if (maxlen < hdr + 2) return 0;  /* at least header + F7 */
    buf[0] = 0xF0;
    buf[1] = 0x00;
    buf[2] = 0x22;
    buf[3] = 0x24;
    buf[4] = 0x35;
    buf[5] = BB_CAST_TYPE;

    /* 7-bit encoded payload */
    int enc_len = encode_7bit(raw, raw_len, buf + hdr, maxlen - hdr - 1);
    buf[hdr + enc_len] = 0xF7;

    /* Update change detection state */
    memcpy(prev_raw, raw, (unsigned)raw_len);
    prev_len = (uint8_t)raw_len;

    return hdr + enc_len + 1;
}

void bb_cast_reset(void)
{
    prev_len = 0;
}
