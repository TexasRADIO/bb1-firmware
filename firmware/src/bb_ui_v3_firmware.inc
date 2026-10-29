/* SPDX-License-Identifier: GPL-3.0-only
 * The v3 interface (claude-code/ui, PR #16) on the boot39 + navigation v2.1 firmware: KIT, PATTERN,
 * PLAY drums and Dillo PLAY are drawn by bb_ui.c from the firmware's own state; every other page,
 * HELP, GLOBAL, the step pages and every overlay stay on the existing renderer.
 *
 * Main loop only. Reads the UI mirror (bb_face.requested), the navigation state (bb_nav, bb_nav_f,
 * bb_nav_l, bb_nav_knob_text), the browser / picker views and the audio side's published words
 * (bb_ticks, playheads, bb_visual_peak, timing). Never writes audio state, never sends a command:
 * input stays with navigation v2.1 unchanged. No heap; state is static (README "Cost").
 * Included by bb_target_ui.c after bb_playbook_face.inc, before bb_target_draw(). */
#ifndef BB_UI_V3
#define BB_UI_V3 1
#endif
#if BB_UI_V3
#define BB_UI_FIRMWARE 1
#define BB_UI_HELP 1                         /* HELP (hold HOME) in the shared frame */
#define BB_UI_ALL_PAGES 1                    /* every player-facing page: STEPS, FX, KNOBS, attract included */
#define BB_UI_HOME 1                         /* STUDIO = the approved two-instrument HOME */
#ifndef BB_UI_BACKDROP
#define BB_UI_BACKDROP 1                     /* the large Armadillo behind every page: 1 layout C, 2 layout A, 0 the mini */
#endif
#define bb_ui_init bb_v3_init                /* (bb_ui_init is the firmware's own UI reset) */
#define bb_ui_frame bb_v3_frame
#define bb_ui_invalidate bb_v3_invalidate
#define bb_ui_page_for bb_v3_page_for
#include "bb_ui_v3/bb_ui.c"
#include "bb_ui_v3_route.h"
#undef bb_ui_init
#undef bb_ui_frame
#undef bb_ui_invalidate
#undef bb_ui_page_for

static bb_ui bb_v3;
static bb_ui_in bb_v3_in;                    /* static, not on the main-loop stack */
static uint8_t bb_v3_shown;                  /* a v3 page is on the screen */
static uint32_t bb_v3_next, bb_v3_beat, bb_v3_step_seen, bb_v3_last_ms;
static uint8_t bb_v3_kpos[4], bb_v3_gout[4], bb_v3_kpage = BB_UI_NO;
static const char *bb_v3_klab[4];
static uint32_t bb_v3_ctx, bb_v3_ctx_t;     /* what the page shows (name, choice) and when it last changed */
#define BB_V3_SETTLE_MS 400u                /* values that follow such a change are not turns */
static char bb_v3_ktext[4][16], bb_v3_pname[24], bb_v3_title[48], bb_v3_sub[48];
static char bb_v3_items[BB_UI_LIST][40], bb_v3_secs[BB_UI_SECTIONS][24], bb_v3_now[48], bb_v3_sec[24];
static char bb_v3_nextname[24];

static void bb_v3_copy(char *d, unsigned n, const char *s)
{
    unsigned k = 0;
    if (s)
        while (s[k] && k + 1u < n) { d[k] = s[k]; k++; }
    d[k] = 0;
}
/* catalogue text into a buffer (printable ASCII; the brand faces fold case themselves) */
static void bb_v3_cat(bb_catalogue_id id, char *out, unsigned n)
{
    const uint8_t *b;
    size_t c, i;
    if (id == BB_CATALOGUE_NONE || !bb_catalogue_text(&bb_factory_catalogue, id, &b, &c)) {
        out[0] = 0;
        return;
    }
    if (c > n - 1u)
        c = n - 1u;
    for (i = 0; i < c; i++)
        out[i] = b[i] >= 32 && b[i] <= 126 ? (char)b[i] : '?';
    out[c] = 0;
}
static void bb_v3_bank_title(const bb_factory_choice *ch, char *out, unsigned n)
{
    bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, ch->bank, ch->group ? "song" : "name"), out, n);
}

/* which v3 page shows now: every navigation state has one (bb_ui_v3_route.h, proven by
 * claude-code/integration/tests/test_routes.c). BB_UI_NO only for the service screens. */
static uint8_t bb_v3_page(void)
{
    return bb_v3_route(&bb_nav, ui.uboot || bb_diag_page);
}

/* the part every page shares: status, knobs (labels and values from navigation v2.1), key LEDs */
static void bb_v3_common(bb_ui_in *in, uint8_t page)
{
    unsigned k, n = 0;
    const char *s = bb_nav_page_name(&bb_nav), *b = bb_nav_bank_name(&bb_nav);
    in->now_ms = fm1_ms;
    in->page = page;
    in->instr = bb_nav_row < 2u ? bb_nav_row : (uint8_t)BB_UI_INST_DILLO;
    in->instr_name = bb_nav_row == 0u ? "KIT A" : bb_nav_row == 1u ? "KIT B" : "DILLO";
    while (*s && n + 1u < sizeof bb_v3_pname) bb_v3_pname[n++] = *s++;
    if (b && b[0] && n + 3u < sizeof bb_v3_pname) {
        bb_v3_pname[n++] = ' '; bb_v3_pname[n++] = ' ';
        while (*b && n + 1u < sizeof bb_v3_pname) bb_v3_pname[n++] = *b++;
    }
    bb_v3_pname[n] = 0;
    in->page_name = bb_v3_pname;
    in->running = bb_face.requested.running;
    in->ticks = bb_ticks;
    in->beat = (uint8_t)(bb_ticks ? (bb_ticks - 1u) / 4u % 4u : 0u);
    in->bpm_x10 = (uint16_t)(bb_face.requested.bpm * 10u);
    in->accent = bb_face.accent;
    in->queued = bb_nav_f.queued;                             /* BB_NAV_Q_* = BB_UI_Q_* */
    {
        uint32_t pk = bb_visual_peak / 64u;
        in->level = (uint8_t)(pk > 255u ? 255u : pk);
    }
    in->fault = (uint8_t)!!bb_sound_pick_audio_failed();
    in->fault_text = "Audio fault: load blocked";
    in->help = (uint8_t)!!bb_help_visible;
    /* the firmware's current message (bb_say: BUSY, REFUSED, CANCELLED BY STOP, TIMED OUT ...) */
    in->notice = bb_hint_until && bb_hint && bb_hint[0] && (int32_t)(fm1_ms - bb_hint_until) < 0 ? bb_hint : 0;
    in->events = 0;
    {   /* any touch of a button, key or encoder wakes attract and counts as input */
        static uint32_t sig_seen;
        uint32_t sig = h_u(h_u(H0, fm1_in.buttons), fm1_in.notes), e;
        for (e = 0; e < FM1_NENC; e++) sig = h_u(sig, fm1_in.enc_last[e]);
        if (sig != sig_seen) { sig_seen = sig; in->events |= BB_EV_INPUT; }
    }
    if (in->running && bb_ticks && (bb_ticks - 1u) / 4u != bb_v3_beat) {
        bb_v3_beat = (bb_ticks - 1u) / 4u;
        in->events |= BB_EV_BEAT;
    }
    in->knob_turning = BB_UI_NO;
    for (k = 0; k < 4u; k++) {
        const char *label = bb_nav_knob_label(k);
        unsigned pos = bb_nav_knob_text(k, bb_v3_ktext[k]);
        bb_ui_knob *kn = &in->knob[k];
        kn->label = label[0] ? label : 0;
        kn->text = bb_v3_ktext[k];
        kn->bar = (uint8_t)(pos > 127u ? 254u : pos * 2u);
        kn->flags = (uint8_t)(!label[0] ? BB_UI_KN_OFF :
                    bb_v3_ktext[k][0] == 'M' && bb_v3_ktext[k][1] == 'U' && bb_v3_ktext[k][2] == 'T' ? BB_UI_KN_MUTE : 0u);
    }
    for (k = 0; k < 27u; k++)
        in->key_led[k] = bb_nav_l.key[k];                     /* BB_NAV_LED_* = BB_UI_LED_* */
    in->keys_down = 0;
    in->keys_red = 0;
}

/* KIT: the picker's list, the highlighted kit / sound as the title, the loaded one marked */
static void bb_v3_kit(bb_ui_in *in)
{
    bb_sound_picker_view view;
    unsigned count = bb_sound_picker_items(&bb_sound_pick), first = (bb_sound_pick.cursor / BB_UI_LIST) * BB_UI_LIST, i;
    int valid = !in->fault && bb_sound_picker_observe(&bb_sound_pick, &bb_sound, &view);
    bb_v3_copy(bb_v3_title, sizeof bb_v3_title, bb_sound_picker_item_name(&bb_sound_pick, bb_sound_pick.cursor));
    in->title = bb_v3_title;
    {
        const char *loaded = valid && view.active_index >= 0 ?
            bb_sound_picker_item_name(&bb_sound_pick, (unsigned)view.active_index) : 0;
        unsigned n = 0;
        const char *p = !valid ? "STATE UNAVAILABLE" : !view.enabled ? "LAYER OFF" : view.muted ? "MUTED" :
                        bb_sound_pick.page == BB_DRUMS ? "LOADED " : "SOUND ";
        while (*p && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *p++;
        if (valid && view.enabled && !view.muted) {
            p = loaded ? loaded : "CUSTOM";
            while (*p && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *p++;
            if (view.edited) { p = " (EDITED)"; while (*p && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *p++; }
        }
        bb_v3_sub[n] = 0;
    }
    in->subtitle = bb_v3_sub;
    in->items = 0;
    for (i = 0; i < BB_UI_LIST && first + i < count; i++) {
        bb_v3_copy(bb_v3_items[i], sizeof bb_v3_items[i], bb_sound_picker_item_name(&bb_sound_pick, first + i));
        in->item[i] = bb_v3_items[i];
        in->items++;
    }
    in->cursor = (uint8_t)(bb_sound_pick.cursor - first);
    in->playing_item = valid && view.active_index >= (int)first && view.active_index < (int)(first + BB_UI_LIST) ?
                       (uint8_t)(view.active_index - (int)first) : (uint8_t)BB_UI_NO;
    in->queued_item = bb_sound_pick.pending && bb_sound_pick.requested >= first && bb_sound_pick.requested < first + BB_UI_LIST ?
                      (uint8_t)(bb_sound_pick.requested - first) : (uint8_t)BB_UI_NO;
    in->loading = bb_sound_pick.pending;
    in->progress = (uint8_t)(bb_ticks ? ((bb_ticks - 1u) % 16u) * 16u : 0u);   /* the ring fills to the bar */
}

#if BB_WITH_BROWSER && BB_WITH_RICH_DRUM_BROWSER && BB_SAMPLE_HOST_INTEGRATION
/* Voice substitutions for the highlighted pattern on the kit it will play with (the chosen NEXT KIT,
 * else the layer's current kit): "SUBST SY>CB +1". Same helper the legacy footer used; cached per
 * selection because it prepares the phrase. Covers sampled-kit fallbacks and drum B's SY/ZAP policy. */
static const char *const bb_v3_voice_code[BB_PHRASE_VOICES] = {
    "BD","SD","LT","MT","HT","LC","MC","HC","RS","CL","CP","MA","CB","CY","OH","HH","RD","SY","ZAP","AC","AC","SH"};
static char bb_v3_subst[24];
static const char *bb_v3_substitutions(const bb_player *p)
{
    static struct { const void *cat; uint16_t bank, part; uint8_t group, kit, machine, valid; } key;
    unsigned kit = BB_SAMPLE_808_ID, count = 0, first = 255, repl = 255;
    if (!p->is_drum || p->target >= BB_MACHINES) return 0;
    if (!bb_library_preview_kit(p, &kit)) kit = BB_SAMPLE_808_ID;
    if (key.valid && key.cat == p->view.catalogue && key.bank == p->view.bank_index && key.part == p->view.part_index &&
        key.group == p->view.group && key.kit == kit && key.machine == p->target)
        return bb_v3_subst[0] ? bb_v3_subst : 0;
    key.cat = p->view.catalogue; key.bank = p->view.bank_index; key.part = p->view.part_index;
    key.group = p->view.group; key.kit = (uint8_t)kit; key.machine = p->target; key.valid = 1;
    bb_v3_subst[0] = 0;
    if (bb_player_preview_substitutions(p, &bb_sound, kit, &count, &first, &repl) && count &&
        first < BB_PHRASE_VOICES && repl < BB_PHRASE_VOICES) {
        unsigned n = 0; const char *t = "SUBST ";
        while (*t) bb_v3_subst[n++] = *t++;
        for (t = bb_v3_voice_code[first]; *t; ) bb_v3_subst[n++] = *t++;
        bb_v3_subst[n++] = '>';
        for (t = bb_v3_voice_code[repl]; *t; ) bb_v3_subst[n++] = *t++;
        if (count > 1u) { bb_v3_subst[n++] = ' '; bb_v3_subst[n++] = '+'; bb_v3_subst[n++] = (char)('0' + (count - 1u > 9u ? 9u : count - 1u)); }
        bb_v3_subst[n] = 0;
    }
    return bb_v3_subst[0] ? bb_v3_subst : 0;
}
#else
static const char *bb_v3_substitutions(const void *p) { (void)p; return 0; }
#endif

#if BB_WITH_BROWSER
/* PATTERN: the browser's bank page as the list, the highlighted pattern as the title, its sections */
static void bb_v3_pattern(bb_ui_in *in)
{
    bb_player *p = bb_library_current();
    bb_factory_choice choice;
    unsigned first, k, n = 0, count = (unsigned)bb_browser_part_count(&p->view), pend = bb_player_pending(p);
    unsigned count_banks = (unsigned)bb_browser_bank_count(&p->view), cur;
    in->title = in->subtitle = 0;
    bb_v3_title[0] = bb_v3_sub[0] = 0;
    if (bb_browser_current(&p->view, &choice)) {
        bb_v3_bank_title(&choice, bb_v3_title, sizeof bb_v3_title);
        if (p->is_drum && p->target < BB_MACHINES && bb_kit_intent[p->target].valid) {
            const char *g = "NEXT KIT: ";
            const char *kit = bb_library_kit_name(bb_kit_intent[p->target].id);
            while (*g && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *g++;
            while (*kit && n + 1u < sizeof bb_v3_sub) bb_v3_sub[n++] = *kit++;
            bb_v3_sub[n] = 0;
        } else {
            const char *g = p->view.group ? "SONG \xB7 " : "ERA \xB7 ";
            while (*g) bb_v3_sub[n++] = *g++;
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, choice.part, "name"), bb_v3_sub + n, sizeof bb_v3_sub - n);
        }
    }
    {   /* the kit's voice substitutions for this pattern, appended so the change is never silent */
        const char *sub = bb_v3_substitutions(p);
        if (sub) {
            unsigned m = 0; while (bb_v3_sub[m]) m++;
            if (m + 3u < sizeof bb_v3_sub) { bb_v3_sub[m++] = ' '; bb_v3_sub[m++] = ' '; }
            while (*sub && m + 1u < sizeof bb_v3_sub) bb_v3_sub[m++] = *sub++;
            bb_v3_sub[m] = 0;
        }
    }
    in->title = bb_v3_title;
    in->subtitle = bb_v3_sub;
    /* the list: five of the page's sixteen around the highlighted bank (keys 1-16 still select) */
    first = p->view.bank_index - p->view.bank_index % BB_UI_LIST;
    in->items = 0;
    for (k = 0; k < BB_UI_LIST; k++) {
        unsigned idx = first + k;
        bb_browser page = p->view;                            /* the 16-key page that holds idx, not the one in view */
        bb_catalogue_id bank;
        page.bank_page = (uint16_t)(idx / BB_BROWSER_BANK_KEYS);
        bank = idx < count_banks ? bb_browser_bank_cell(&page, idx % BB_BROWSER_BANK_KEYS) : BB_CATALOGUE_NONE;
        if (bank == BB_CATALOGUE_NONE)
            break;
        bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, bank, p->view.group ? "song" : "name"), bb_v3_items[k],
                  sizeof bb_v3_items[k]);
        in->item[k] = bb_v3_items[k];
        in->items++;
    }
    in->cursor = (uint8_t)(p->view.bank_index - first);
    in->playing_item = p->playing.ticket && p->playing.choice.group == p->view.group &&
                       p->playing.choice.bank_index >= first && p->playing.choice.bank_index < first + BB_UI_LIST ?
                       (uint8_t)(p->playing.choice.bank_index - first) : (uint8_t)BB_UI_NO;
    in->queued_item = pend && p->request.choice.group == p->view.group && p->request.choice.bank_index >= first &&
                      p->request.choice.bank_index < first + BB_UI_LIST ?
                      (uint8_t)(p->request.choice.bank_index - first) : (uint8_t)BB_UI_NO;
    in->progress = (uint8_t)(bb_ticks ? ((bb_ticks - 1u) % 16u) * 16u : 0u);
    /* the highlighted pattern's sections (up to eight from the section page in use) */
    first = bb_nav_section_page(&bb_nav) * BB_NAV_SECTION_KEYS;
    in->sections = 0;
    if (bb_browser_current(&p->view, &choice)) {
        bb_catalogue_id parts = bb_catalogue_find(&bb_factory_catalogue, choice.bank, "parts");
        for (k = 0; k < BB_UI_SECTIONS && first + k < count; k++) {
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, bb_catalogue_at(&bb_factory_catalogue, parts, first + k), "name"),
                      bb_v3_secs[k], sizeof bb_v3_secs[k]);
            in->sec_name[k] = bb_v3_secs[k];
            in->sec_bars[k] = 1;
            in->sections++;
        }
    }
    cur = bb_nav_section_playing(p);                          /* the playing section; the cursor only when browsing */
    if (cur == BB_NAV_NONE_IDX)
        cur = p->view.part_index;
    in->sec_cur = cur >= first && cur < first + in->sections ? (uint8_t)(cur - first) : (uint8_t)BB_UI_NO;
    in->sec_next = (bb_nav_f.queued & BB_NAV_Q_SECTION) && bb_nav_f.section_queued != BB_NAV_NONE_IDX &&
                   bb_nav_f.section_queued >= first && bb_nav_f.section_queued < first + in->sections ?
                   (uint8_t)(bb_nav_f.section_queued - first) : (uint8_t)BB_UI_NO;
    in->sec_pos = 0;
    in->fill = bb_nav_f.fill_on;
    in->fill_beats = 0;
}
#endif

/* the drum layer's steps for one bar, by group (gg_808_sound: the firmware's own group map) */
static void bb_v3_lanes(bb_ui_in *in, unsigned bar)
{
    const bb_machine *m = &bb_face.requested.drums[bb_face.machine];
    unsigned k, t, g;
    for (g = 0; g < 4u; g++)
        in->gsteps[g] = in->gacc[g] = 0;
    for (k = 0; k < 16u; k++) {
        unsigned step = bar * 16u + k;
#if BB_WITH_RICH_DRUM_BROWSER
        if (bb_rich_active()) {
            for (t = 0; t < 17u; t++) {
                bb_phrase_cell cell = {0};
                int gg;
                if (!bb_phrase_view_cell(&bb_rich_views[bb_face.machine], bb_rich_sources[t], step, &cell) || !cell.hits)
                    continue;
                if ((gg = gg_808_sound(bb_rich_sounds[t])) < 0)
                    continue;
                in->gsteps[gg] |= (uint16_t)(1u << k);
                if (cell.accents)
                    in->gacc[gg] |= (uint16_t)(1u << k);
            }
            continue;
        }
#endif
        if (step >= m->length)
            continue;
        for (t = 0; t < BB_DRUM_TRACKS; t++) {
            int gg;
            if (!((m->steps[step].hits >> t) & 1u))
                continue;
            if ((gg = gg_808_sound(bb_drum_sound_index(&m->sound, t))) < 0)
                continue;
            in->gsteps[gg] |= (uint16_t)(1u << k);
            if ((m->steps[step].accents >> t) & 1u)
                in->gacc[gg] |= (uint16_t)(1u << k);
        }
    }
}

/* PLAY drums: what is playing, the four group lanes, OUT and DSP */
static void bb_v3_play_drums(bb_ui_in *in)
{
    unsigned machine = bb_face.machine, length, pos, g, dt = fm1_ms - bb_v3_last_ms;
    bb_group_view view;
#if BB_WITH_BROWSER
    bb_player *p = bb_library_current();
    const bb_factory_choice *now = p->playing.ticket ? &p->playing.choice : 0;
    bb_factory_choice cur;
    if (!now && bb_browser_current(&p->view, &cur))
        now = &cur;
    bb_v3_now[0] = bb_v3_sec[0] = bb_v3_nextname[0] = 0;
    if (now) {
        bb_v3_bank_title(now, bb_v3_now, sizeof bb_v3_now);
        bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, now->part, "name"), bb_v3_sec, sizeof bb_v3_sec);
    }
    if (bb_player_pending(p)) {
        if (bb_nav_f.queued & BB_NAV_Q_SECTION)
            bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, p->request.choice.part, "name"), bb_v3_nextname,
                      sizeof bb_v3_nextname);
        else
            bb_v3_bank_title(&p->request.choice, bb_v3_nextname, sizeof bb_v3_nextname);
    }
#endif
    in->pattern_name = bb_v3_now[0] ? bb_v3_now : "CURRENT PATTERN";
    in->section_name = bb_v3_sec;
    in->next_name = bb_v3_nextname[0] ? bb_v3_nextname : 0;
    /* position: the phrase playhead (rich) or the layer's playhead, in bars of 16 */
    length = bb_drum_visible_length(&bb_face.requested.drums[machine]);
#if BB_WITH_RICH_DRUM_BROWSER
    pos = bb_rich_active() ? bb_audio_phrase_playhead(&bb_sound, machine) : bb_playhead[machine];
#else
    pos = bb_playhead[machine];
#endif
    if (length)
        pos %= length;
    in->step = (uint8_t)(pos % 16u);
    in->bar = (uint8_t)(pos / 16u + 1u);
    in->bars = (uint8_t)(length > 16u ? (length + 15u) / 16u : 0u);
    if (!in->bars)
        in->bar = 0;
    bb_v3_lanes(in, pos / 16u);
    /* group levels and mutes as the K1-K4 route has them; live level: each group's hit at its
     * level, falling over ~120 ms (the firmware publishes no per-group peak; README §6) */
    if (bb_group_input(&bb_sound.commands, bb_sound.groups, machine, BB_GROUP_READ, 0, 0, &view) != BB_GROUP_REFUSED) {
        for (g = 0; g < 4u; g++)
            in->group_level[g] = (uint8_t)(view.level[g] > 127u ? 254u : view.level[g] * 2u);
        in->group_muted = (uint8_t)(view.muted & 15u);
    }
    for (g = 0; g < 4u; g++) {
        unsigned v = bb_v3_gout[g];
        v = dt >= 120u ? 0u : v * (120u - dt) / 120u;
        if (in->running && pos != bb_v3_step_seen && (in->gsteps[g] >> in->step & 1u) && !(in->group_muted >> g & 1u))
            v = in->group_level[g];
        bb_v3_gout[g] = (uint8_t)v;
        in->group_out[g] = (uint8_t)v;
    }
    bb_v3_step_seen = pos;
    {
        uint32_t pk = bb_visual_peak / 128u * 2u;            /* one published peak: L = R (README §6) */
        in->out_peak[0] = in->out_peak[1] = (uint8_t)(pk > 255u ? 255u : pk);
    }
#ifdef BB_DIAGNOSTICS_WHOLE_HALF
    {
        uint32_t total, budget, d;
        uintptr_t token = bb_target_enter(0);
        total = bb_target_timing.last_total_ticks;
        budget = bb_target_timing.budget_ticks;
        bb_target_leave(0, token);
        d = bb_deadline_percent(total, budget);
        in->dsp = (uint8_t)(d >= 100u ? 255u : d * 255u / 100u);
    }
#else
    in->dsp = (uint8_t)(BB_METER_CPU_Q8 > 255u ? 255u : BB_METER_CPU_Q8);
#endif
}

/* Dillo PLAY: the patch, the sequence as a piano roll, the filter */
static void bb_v3_play_dillo(bb_ui_in *in)
{
    const bb_sequence *seq = &bb_face.requested.synths[0];
    unsigned k, lo = 255, len = seq->length ? seq->length : 16u, lim0, lim1, v;
    in->patch = bb_patch_from_scene[0] == 2 ? "Era sound" : bb_patch_from_scene[0] ? "Scene sound" :
                bb_dillo_bank[bb_patch_index[0] % BB_DILLO_PATCHES].name;
    in->scale = bb_nav_scale_hint();
    if (!in->scale)
        in->scale = bb_patch_edited[0] ? "EDITED" : "";
    for (k = 0; k < 16u && k < len; k++)
        if (seq->notes[k].gate && seq->notes[k].note < lo)
            lo = seq->notes[k].note;
    in->note_acc = in->note_slide = 0;
    for (k = 0; k < 16u; k++) {
        const bb_note *n = &seq->notes[k];
        in->note[k] = (int8_t)(k < len && n->gate ? (int)(n->note - lo) : -1);
        if (in->note[k] > 12) in->note[k] = (int8_t)(12 + (in->note[k] - 12) % 12 / 6);
        if (n->accent) in->note_acc |= (uint16_t)(1u << k);
        if (n->slide) in->note_slide |= (uint16_t)(1u << k);
    }
    in->step = (uint8_t)(bb_synth_playhead[0] % 16u);
    v = bb_synth_parameter_get(&bb_face.requested, 0, BB_PARAM_CUTOFF);
    if (bb_synth_parameter_limits(0, BB_PARAM_CUTOFF, &lim0, &lim1) && lim1 > lim0)
        in->cutoff = (uint8_t)((v < lim0 ? 0u : v > lim1 ? lim1 - lim0 : v - lim0) * 255u / (lim1 - lim0));
    v = bb_synth_parameter_get(&bb_face.requested, 0, BB_PARAM_RESONANCE);
    if (bb_synth_parameter_limits(0, BB_PARAM_RESONANCE, &lim0, &lim1) && lim1 > lim0)
        in->reso = (uint8_t)((v < lim0 ? 0u : v > lim1 ? lim1 - lim0 : v - lim0) * 255u / (lim1 - lim0));
    in->octave = bb_nav.key_octave;
}

static char bb_v3_home_pattern[3][48], bb_v3_home_next[3][48], bb_v3_home_section[3][24];
static bb_catalogue_id bb_v3_home_bank[3], bb_v3_home_part[3], bb_v3_home_pending[3], bb_v3_home_pending_part[3];
static uint8_t bb_v3_home_group[3], bb_v3_home_pending_group[3];
static void bb_v3_home(bb_ui_in *in)
{
    unsigned r;
    for (r = 0; r < 3u; r++) {
        bb_player *p = r < 2u ? &bb_drum_library[r] : &bb_library[0];
        bb_ui_lane *l = &in->lane[r];
        bb_catalogue_id bank = p->playing.ticket ? p->playing.choice.bank : BB_CATALOGUE_NONE;
        bb_catalogue_id part = p->playing.ticket ? p->playing.choice.part : BB_CATALOGUE_NONE;
        bb_catalogue_id pending = bb_player_pending(p) ? p->request.choice.bank : BB_CATALOGUE_NONE;
        bb_catalogue_id pending_part = pending != BB_CATALOGUE_NONE ? p->request.choice.part : BB_CATALOGUE_NONE;
        uint8_t group = bank != BB_CATALOGUE_NONE ? p->playing.choice.group : 0u;
        uint8_t pending_group = pending != BB_CATALOGUE_NONE ? p->request.choice.group : 0u;
        if (bb_v3_home_part[r] != part || (part != BB_CATALOGUE_NONE && !bb_v3_home_section[r][0])) {
            if (part != BB_CATALOGUE_NONE)
                bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, part, "name"),
                          bb_v3_home_section[r], sizeof bb_v3_home_section[r]);
            else
                bb_v3_copy(bb_v3_home_section[r], sizeof bb_v3_home_section[r], "");
            bb_v3_home_part[r] = part;
        }
        l->name = bb_v3_home_section[r];
        l->flags = (uint8_t)((r == bb_nav_row ? BB_UI_LN_FOCUS : 0u) |
                            (r < 2u ? (!bb_face.requested.drums[r].enabled ? BB_UI_LN_OFF : 0u) |
                                      (bb_face.requested.drums[r].muted ? BB_UI_LN_MUTED : 0u) :
                                      (bb_face.requested.synths[0].muted ? BB_UI_LN_MUTED : 0u)));
        l->part = r < 2u ? bb_ui_kit_name(r) :
            bb_dillo_bank[bb_patch_index[0] % BB_DILLO_PATCHES].name;
        if (bb_v3_home_bank[r] != bank || bb_v3_home_group[r] != group || !bb_v3_home_pattern[r][0]) {
            if (bank != BB_CATALOGUE_NONE)
                bb_v3_bank_title(&p->playing.choice, bb_v3_home_pattern[r], sizeof bb_v3_home_pattern[r]);
            else
                bb_v3_copy(bb_v3_home_pattern[r], sizeof bb_v3_home_pattern[r], "CHOOSE PATTERN");
            bb_v3_home_bank[r] = bank;
            bb_v3_home_group[r] = group;
        }
        if (bb_v3_home_pending[r] != pending || bb_v3_home_pending_part[r] != pending_part ||
            bb_v3_home_pending_group[r] != pending_group) {
            if (pending != BB_CATALOGUE_NONE) {
                if (pending == bank)
                    bb_v3_cat(bb_catalogue_find(&bb_factory_catalogue, pending_part, "name"),
                              bb_v3_home_next[r], sizeof bb_v3_home_next[r]);
                else
                    bb_v3_bank_title(&p->request.choice, bb_v3_home_next[r], sizeof bb_v3_home_next[r]);
            } else
                bb_v3_home_next[r][0] = 0;
            bb_v3_home_pending[r] = pending;
            bb_v3_home_pending_part[r] = pending_part;
            bb_v3_home_pending_group[r] = pending_group;
        }
        l->pattern = bb_v3_home_pattern[r];
        l->next = bb_v3_home_next[r][0] ? bb_v3_home_next[r] : 0;
    }
}

/* STEPS (drums): every lane x 16 steps of the shown bank, the selected voice big. Rich phrases
 * fold their 17 source voices onto the firmware's own 11 lanes (bb_rich_lanes); the selected lane
 * shows exactly the selected source. */
static void bb_v3_steps(bb_ui_in *in)
{
    const bb_machine *m = &bb_face.requested.drums[bb_face.machine];
    unsigned len = bb_nav_length(), k, v, base = bb_face.bank * 16u;
    unsigned head = bb_playhead[bb_face.machine];
    in->voice_name = bb_ui_voice_name();
    in->bank = bb_face.bank;
    in->banks = (uint8_t)(len ? (len + 15u) / 16u : 1u);
    in->step = (uint8_t)(head >= base && head < base + 16u ? head - base : 16u);
#if BB_WITH_RICH_DRUM_BROWSER
    if (bb_rich_active()) {
        unsigned sel = bb_rich_voices[bb_face.machine] % 17u;
        in->voice = bb_rich_lanes[sel];
        for (v = 0; v < 17u; v++)
            for (k = 0; k < 16u; k++) {
                bb_phrase_cell cell = {0};
                if (base + k < len && bb_phrase_view_cell(&bb_rich_views[bb_face.machine], bb_rich_sources[v], base + k, &cell) &&
                    cell.hits) {
                    unsigned lane = bb_rich_lanes[v];
                    if (lane == in->voice && v != sel) continue;          /* the big row is the selected source */
                    in->vsteps[lane] |= (uint16_t)(1u << k);
                    if (cell.accents) in->vacc[lane] |= (uint16_t)(1u << k);
                }
            }
        in->voice_name = bb_rich_names[sel];
        return;
    }
#endif
    in->voice = (uint8_t)(bb_face.voice < 11u ? bb_face.voice : 0u);
    for (k = 0; k < 16u; k++) {
        unsigned step = base + k;
        if (step >= m->length) break;
        for (v = 0; v < 11u; v++) {
            if (m->steps[step].hits >> v & 1u) in->vsteps[v] |= (uint16_t)(1u << k);
            if (m->steps[step].accents >> v & 1u) in->vacc[v] |= (uint16_t)(1u << k);
        }
    }
}

#if BB_WITH_EFFECTS
/* FX: the echo machine (ECHO, TAPE, MOTION banks) from the requested effect parameters */
static unsigned bb_v3_fx_scaled(unsigned p)
{
    unsigned lo = 0, hi = 0, v = bb_fx_parameter_get(&bb_face.requested, p);
    bb_fx_parameter_limits(p, &lo, &hi);
    return hi > lo ? (v < lo ? 0u : v > hi ? 255u : (v - lo) * 255u / (hi - lo)) : 0u;
}
static void bb_v3_fx(bb_ui_in *in)
{
    in->echo_ms = (uint16_t)bb_fx_parameter_get(&bb_face.requested, BB_FX_ECHO_MS);
    in->repeats = (uint8_t)bb_v3_fx_scaled(BB_FX_ECHO_FEEDBACK);
    in->heads = (uint8_t)(bb_fx_parameter_get(&bb_face.requested, BB_FX_ECHO_HEADS) & 7u);
    in->banks_fx = bb_nav_bank_count(&bb_nav, BB_NAV_FX);
    in->bank_fx = bb_nav.bank;
}
#endif

/* SOUND, SAVE, EVOLVE, GLOBAL, SPRING, SENDS: what is being set as the title, what EDIT does as the
 * grey line, the four values as meters over their own knobs (labels and values: navigation v2.1) */
static void bb_v3_knobs(bb_ui_in *in)
{
    const char *edit = bb_nav_edit_label(&bb_nav, &bb_nav_f);
    unsigned n = 0;
    const char *t = "";
    switch (bb_nav.page) {
    case BB_NAV_SOUND:
        t = bb_nav_is_drum() ? bb_ui_voice_name() : bb_patch_from_scene[0] == 2 ? "Era sound" :
            bb_patch_from_scene[0] ? "Scene sound" : bb_dillo_bank[bb_patch_index[0] % BB_DILLO_PATCHES].name;
        break;
#if BB_WITH_SESSION
    case BB_NAV_SAVE: {
        char *o = bb_v3_title; const char *w = "SCENE ";
        while (*w) o[n++] = *w++;
        o[n++] = (char)('1' + bb_scene_slot % 9u);
        if (bb_claim_armed(&bb_session_claim, bb_scene_slot)) {   /* other data: the held-SAVE prompt */
            w = " HAS OTHER DATA";
            while (*w) o[n++] = *w++;
            edit = bb_claim_prompt(&bb_session_claim);
        }
        o[n] = 0;
        if (bb_face.requested.running) edit = "STOP TO STORE OR LOAD";
        break;
    }
#endif
    case BB_NAV_EVOLVE: t = bb_nav_row == 0u ? "EVOLVE  DRUM A" : bb_nav_row == 1u ? "EVOLVE  DRUM B" : "EVOLVE  DILLO"; break;
    case BB_NAV_GLOBAL: t = "GLOBAL"; break;
    default: t = bb_nav_bank_name(&bb_nav); break;                  /* FX: SPRING / SENDS */
    }
    if (!n) bb_v3_copy(bb_v3_title, sizeof bb_v3_title, t ? t : "");
    in->title = bb_v3_title;
    bb_v3_copy(bb_v3_sub, sizeof bb_v3_sub, edit && edit[0] ? edit : "");
    in->subtitle = bb_v3_sub[0] ? bb_v3_sub : 0;
}

/* bb_target_draw: draw this frame with v3; 0 = a service screen (update boot, diagnostics) */
static int bb_v3_draw(void)
{
    uint8_t page = bb_v3_page();
    bb_ui_in *in = &bb_v3_in;
    if (page == BB_UI_NO) {
        if (bb_v3_shown) {                                    /* back to the existing renderer: all of it */
            bb_v3_shown = 0;
            bb_dirty = 1;
        }
        return 0;
    }
    if (!bb_v3_shown) {
        bb_v3_init(&bb_v3, fm1_ms, bb_face.requested.running);
        bb_v3_invalidate(&bb_v3);
    } else if ((int32_t)(fm1_ms - bb_v3_next) < 0) {
        return 1;                                             /* ~30 frames a second */
    }
    bb_v3_next = fm1_ms + 33u;
    if (ux.light || ux.mono) {                                /* text ramps on a dark, colour palette */
        ux.light = 0;
        ux.mono = 0;
        ux.gen++;
    }
    memset(in, 0, sizeof *in);
    bb_v3_common(in, page);
    switch (page) {
    case BB_UI_STUDIO: bb_v3_home(in); break;
    case BB_UI_KIT:
        if (bb_sound_pick_visible()) bb_v3_kit(in);
        else { bb_v3_copy(bb_v3_title, sizeof bb_v3_title, "SOUND LIST NOT READY"); in->title = bb_v3_title; }
        break;
#if BB_WITH_BROWSER
    case BB_UI_PATTERN:
        if (bb_library_page && bb_library_current()->ready) bb_v3_pattern(in);
        else { bb_v3_copy(bb_v3_title, sizeof bb_v3_title, "PATTERNS NOT READY"); in->title = bb_v3_title; }
        break;
#endif
    case BB_UI_PLAY_DRUMS: bb_v3_play_drums(in); break;
    case BB_UI_STEPS: bb_v3_steps(in); break;
#if BB_WITH_EFFECTS
    case BB_UI_FX: bb_v3_fx(in); break;
#endif
    case BB_UI_KNOBS: bb_v3_knobs(in); break;
    default: bb_v3_play_dillo(in); break;
    }
    /* a turned knob: its pop-up. A value that changes because the page, the focus (labels) or the
     * chosen kit / pattern / patch changed is not a turn; only the same knob on the same thing is */
    {
        unsigned k;
        uint32_t ctx = h_s(h_s(h_s(h_u(H0, page), in->title), in->pattern_name), in->patch);
        for (k = 0; k < 4u; k++) {
            const bb_ui_knob *kn = &in->knob[k];
            if (bb_v3_shown && page == bb_v3_kpage && ctx == bb_v3_ctx && fm1_ms - bb_v3_ctx_t >= BB_V3_SETTLE_MS &&
                kn->label && kn->label == bb_v3_klab[k] && kn->bar != bb_v3_kpos[k]) {
                in->events |= BB_EV_KNOB;
                in->ev_knob = (uint8_t)k;
            }
            bb_v3_kpos[k] = kn->bar;
            bb_v3_klab[k] = kn->label;
        }
        if (page != bb_v3_kpage || ctx != bb_v3_ctx || !bb_v3_shown)
            bb_v3_ctx_t = fm1_ms;
        bb_v3_kpage = page;
        bb_v3_ctx = ctx;
    }
    bb_v3_last_ms = fm1_ms;
    bb_v3_shown = 1;
    bb_dirty = 0;
    (void)bb_v3_frame(&bb_v3, in);
    return 1;
}
#else
static int bb_v3_draw(void) { return 0; }
#endif
