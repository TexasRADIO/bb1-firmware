/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.2 playability: what the playing pages show (bb_ui_v3/bb_ui_play.h), from main-loop state only.
 * Included by bb_ui_v3_firmware.inc after its page fillers; called from bb_v3_draw before the frame.
 *
 * Reads: the UI mirror (bb_face.requested: patterns, sequences, sounds, effect values), the navigation model
 * (bb_nav, bb_nav_f, its key roles), the keys (fm1_in.notes), the held-note map (bb_nav_note_on) and the playheads
 * the audio adapter already publishes to the main loop (bb_playhead, bb_synth_playhead, the phrase playhead v3 reads).
 * Writes nothing outside this file. The audio side publishes no per-voice hit to the main loop (README section 6 of
 * the v3 module), so a pad flashes when the step position reaches a hit of the shown pattern on that track (as v3's
 * group meters do), or when its key is pressed. */
#if BB_UI_V3 && BB_UI_PLAY
static bb_ui_play bb_pl __attribute__((section(".pool")));
static uint32_t bb_pl_until[BB_PL_PADS] __attribute__((section(".pool")));   /* each pad lit until */
static uint32_t bb_pl_pos __attribute__((section(".pool")));                  /* the step last flashed (+1) */
static uint32_t bb_pl_keys __attribute__((section(".pool")));                 /* logical keys held last frame */
#define BB_PL_FLASH_MS 120u

static uint8_t bb_pl_255(unsigned v, unsigned lo, unsigned hi)
{
    return (uint8_t)(hi > lo ? (v < lo ? 0u : v > hi ? 255u : (v - lo) * 255u / (hi - lo)) : 0u);
}
static uint8_t bb_pl_synth(unsigned sy, unsigned p)
{
    unsigned lo = 0, hi = 0;
    return bb_synth_parameter_limits(sy, p, &lo, &hi) ? bb_pl_255(bb_synth_parameter_get(&bb_face.requested, sy, p), lo, hi) : 0u;
}
/* the tracks that strike at step pos of the shown drum pattern (the rich phrase folded onto the 11 lanes) */
static PL_NOINLINE uint32_t bb_pl_hits(unsigned machine, unsigned pos)
{
    const bb_machine *m = &bb_face.requested.drums[machine];
    uint32_t hits = 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (bb_rich_active()) {
        for (unsigned t = 0; t < 17u; t++) {
            bb_phrase_cell cell = {0};
            if (bb_phrase_view_cell(bb_rich_view(machine), bb_rich_sources[t], pos, &cell) && cell.hits)
                hits |= 1u << bb_rich_lanes[t];
        }
        return hits;
    }
#endif
    return pos < m->length ? m->steps[pos].hits : 0u;
}
static PL_NOINLINE void bb_pl_fill(bb_ui_in *in, uint8_t page)
{
    bb_ui_play *p = &bb_pl;
    const unsigned machine = bb_face.machine, sy = bb_nav_synth();
    const bb_machine *m = &bb_face.requested.drums[machine];
    uint32_t keys = bb_nav_keys_logical(fm1_in.notes), down = keys & ~bb_pl_keys, now = fm1_ms;
    unsigned k;
    uint8_t a;
    bb_pl_keys = keys;
    memset(p, 0, sizeof *p);
    for (k = 0; k < 27u; k++) {                                          /* what each key plays / sets */
        uint8_t r = bb_nav_key_role(&bb_nav, (uint8_t)k, 0, &a);
        if (r == BB_NAV_K_NOTE || r == BB_NAV_K_STEP_PITCH) p->key_note[k] = a;
        if (r == BB_NAV_K_PAD && (down >> k & 1u)) bb_pl_until[k] = now + BB_PL_FLASH_MS;   /* a finger on a pad */
    }
    p->instr_muted = bb_nav_f.instr_muted;
    p->focus = (uint8_t)(bb_nav_row < 2u ? bb_nav_row : 2u);
    if (page == BB_UI_PLAY_DRUMS) {
        unsigned length = bb_drum_visible_length(m), pos;
#if BB_WITH_RICH_DRUM_BROWSER
        pos = bb_rich_active() ? bb_audio_phrase_playhead(&bb_sound, machine) : bb_playhead[machine];
#else
        pos = bb_playhead[machine];
#endif
        if (length) pos %= length;
        if (in->running && pos + 1u != bb_pl_pos) {                     /* the step moved: its hits sound */
            uint32_t hits = bb_pl_hits(machine, pos);
            for (k = 0; k < BB_PL_PADS; k++)
                if (hits >> k & 1u) bb_pl_until[k] = now + BB_PL_FLASH_MS;
        }
        bb_pl_pos = in->running ? pos + 1u : 0u;
        for (k = 0; k < BB_PL_PADS; k++) {
            int g = gg_808_sound(bb_drum_sound_index(&m->sound, k));
            p->pad_name[k] = bb_kit_family[machine] == 1 ? bb909_sequence_voice_name(k) : bb_drum_voice_name(&m->sound, k);
            p->pad_group[k] = (uint8_t)(g >= 0 && g < 4 ? g : 0xFF);
            if ((int32_t)(bb_pl_until[k] - now) > 0) p->pad_lit |= (uint16_t)(1u << k);
        }
    } else if (page == BB_UI_PLAY_DILLO) {
        const bb_sequence *seq = &bb_face.requested.synths[sy];
        unsigned base = bb_face.bank * 16u, head = bb_synth_playhead[sy], cur = 0xFFu;
        p->keys = bb_nav.play_keys == BB_NAV_PLAY_LIVE;
        in->envmod = bb_pl_synth(sy, BB_PARAM_ENVELOPE);
        in->note_acc = in->note_slide = 0;
        for (k = 0; k < 16u; k++) {                                      /* the visible bank of the sheet */
            const bb_note *n;
            if (base + k >= seq->length || !(n = &seq->notes[base + k])->gate) continue;
            p->step_gate |= (uint16_t)(1u << k);
            p->step_note[k] = n->note;
            if (n->accent) in->note_acc |= (uint16_t)(1u << k);
            if (n->slide) in->note_slide |= (uint16_t)(1u << k);
        }
        in->step = (uint8_t)(head >= base && head < base + 16u ? head - base : 16u);
        if (bb_selected_note[sy] >= base && bb_selected_note[sy] < base + 16u)
            p->step_held = (uint16_t)(1u << (bb_selected_note[sy] - base));
        if (in->running && head < seq->length && seq->notes[head].gate) cur = seq->notes[head].note;
        for (k = 0; k < 27u; k++)
            if (p->key_note[k] && (bb_nav_note_bit(bb_nav_note_on, p->key_note[k]) || p->key_note[k] == cur))
                p->keys_sounding |= 1u << k;
#if BB_WITH_EFFECTS
    } else if (page == BB_UI_FX) {
        unsigned lo;
        p->echo_ms = (uint16_t)bb_fx_parameter_get(&bb_face.requested, BB_FX_ECHO_MS);
        static const uint8_t P[9] = {BB_FX_ECHO_FEEDBACK, BB_FX_ECHO_WET, BB_FX_ECHO_TONE, BB_FX_ECHO_DRIVE, BB_FX_ECHO_WEAR,
                                     BB_FX_ECHO_MOTION, BB_FX_SPRING_SIZE, BB_FX_SPRING_DAMP, BB_FX_SPRING_WET};
        for (k = 0; k < 9u; k++) p->fx[k] = (uint8_t)bb_v3_fx_scaled(P[k]);
        for (k = 0; k < 2u; k++) {                                       /* SENDS: the shown voice of each kit, Dillo */
            const bb_drum_sound *d = &bb_face.requested.drums[k].sound;
            unsigned e = bb_drum_parameter_get(d, bb_face.voice, BB_DRUM_ECHO), s = bb_drum_parameter_get(d, bb_face.voice, BB_DRUM_SPRING);
            p->send[k][0] = (uint8_t)(e > 127u ? 255u : e * 2u);
            p->send[k][1] = (uint8_t)(s > 127u ? 255u : s * 2u);
        }
        lo = sy ? BB_FX_TELEGRAM_ECHO : BB_FX_DILLO_ECHO;
        p->send[2][0] = (uint8_t)bb_v3_fx_scaled(lo);
        p->send[2][1] = (uint8_t)bb_v3_fx_scaled(lo + 1u);
#endif
    }
    bb_ui_play_set(p);
}
#else
#define bb_pl_fill(in, page) ((void)0)
#endif
