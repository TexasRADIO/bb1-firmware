/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 RC1: KEEP on the Dillo PATCH page is never claimed before it happens (Duke #64 6592267384).
 * Compiles the REAL firmware adapter, firmware/src/bb_dillo_audition_firmware.inc (bb_aud_keep, bb_aud_poll,
 * bb_aud_load, bb_aud_detent), with the real bb_dillo_audition.h, as Duke did. Only the picker's observe / commit
 * outcomes, the owner's apply and the notice sink are modelled: a command queue with a given room, one picker request
 * in flight at a time (BB_SP_BUSY), REFUSED when the queue is full, and the owner applying the request on a drain.
 *  k1 refused: KEEP with the queue full: no SOUND KEPT, the audition and the cursor stay; retried every scan; once the
 *     queue has room the patch loads, and SOUND KEPT shows only after the owner plays it.
 *  k2 pending: KEEP while another picker request is in flight: owed; that request resolves; then the cursor's patch
 *     loads, then SOUND KEPT. Nothing is discarded.
 *  k3 plain KEEP: the load goes in the same scan; SOUND KEPT after the owner applies it; exactly once.
 *  k4 nothing owed (no settle armed, nothing in flight): SOUND KEPT at once, no command.
 *  k5 HOME or leaving the page while KEEP is owed: dropped, never SOUND KEPT; HOME still restores.
 *  k6 the owner drops the accepted request (it plays something else): KEEP loads again; no false SOUND KEPT.
 *  RC2 (Duke #64 6599173185): SOUND KEPT only after the owner is OBSERVED playing the cursor's patch, on every path.
 *  k7 Duke's case: a settle accepted (pending, armed 0), KEEP while it is in flight, the owner drops it: no claim.
 *  k8 every KEEP entry x every owner outcome: entries = nothing owed, stale cursor after a dropped settle, settle
 *     armed, settle accepted in flight, another request in flight, queue refused; outcomes = the owner applies,
 *     drops, or plays another patch, each scan. A claim is only ever made while the cursor's patch plays, at most
 *     once, and once the owner applies, KEEP completes.
 *  k9 an edited sound: settle away and back onto its patch never reloads it (the edit is kept); KEEP keeps it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bb_nav.h"
/* ---------------------------------------------------------------- the adapter's firmware environment (model) */
enum { BB_SP_OK, BB_SP_BUSY, BB_SP_INVALID, BB_SP_UNCHANGED, BB_SP_REFUSED };
enum { BB_CMD_DILLO_PATCH = 1, BB_CMD_SYNTH_PARAMETER = 2 };
typedef int bb_command_type;
typedef struct { int active_index; } bb_sound_picker_view;
typedef struct { uint8_t cursor, pending; } bb_sound_picker;
static bb_sound_picker bb_sound_pick;
static int bb_sound, bb_sound_pick_kits;
static struct { int requested; } bb_face;
static bb_nav_state bb_nav;
static int drum_focus, bb_dirty;
static uint32_t fm1_ms = 1000;
static uint8_t bb_patch_index[2], bb_patch_edited[2], bb_patch_from_scene[2];
static unsigned room, cmds, params, says_kept, says_restored, requested, observe_ok = 1, busy;
static int active, edited;
static uint16_t param[8];
static int bb_nav_is_drum(void) { return drum_focus; }
static int bb_sound_pick_visible(void) { return 1; }
static void bb_say(const char *t) { if (!strcmp(t, "SOUND KEPT")) says_kept++; if (!strcmp(t, "SOUND RESTORED")) says_restored++; }
static unsigned bb_synth_parameter_get(const int *f, unsigned synth, unsigned p) { (void)f; (void)synth; return param[p & 7u]; }
static int bb_send_synth(unsigned synth, bb_command_type type, unsigned value, unsigned p)
{
    (void)synth; if (!room) return 0; room--;
    if (type == BB_CMD_DILLO_PATCH) { cmds++; active = (int)value; edited = 0; } else { params++; param[p & 7u] = (uint16_t)value; }
    return 1;
}
static int bb_sound_picker_observe(const bb_sound_picker *p, const int *a, bb_sound_picker_view *v)
{
    (void)p; (void)a; if (!observe_ok) return 0; v->active_index = edited ? -1 : active; return 1;
}
static int bb_sound_picker_commit(bb_sound_picker *p, const int *a, const int *k)
{
    (void)a; (void)k;
    if (!observe_ok) return BB_SP_INVALID;
    if (p->pending) { busy++; return BB_SP_BUSY; }   /* the audition must not even try while one is in flight */
    if (active == p->cursor && !edited) return BB_SP_UNCHANGED;
    if (!room) return BB_SP_REFUSED;
    room--; cmds++; requested = p->cursor; p->pending = 1; return BB_SP_OK;
}
/* the owner applies the request; the picker poll then labels the patch (bb_patch_index), as the firmware does */
static void owner_drain(void) { if (bb_sound_pick.pending) { active = (int)requested; edited = 0; bb_sound_pick.pending = 0; bb_patch_index[0] = (uint8_t)active; } }
static void owner_drop(void) { bb_sound_pick.pending = 0; }   /* the request ends without being applied */
#include "bb_dillo_audition_firmware.inc"
/* ---------------------------------------------------------------- harness */
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void world(void)
{
    memset(&bb_aud, 0, sizeof bb_aud); memset(&bb_sound_pick, 0, sizeof bb_sound_pick);
    bb_nav_init(&bb_nav, BB_NAV_STUDIO, 600); bb_nav.page = BB_NAV_KIT; drum_focus = 0;
    room = 1000; cmds = params = says_kept = says_restored = busy = 0; observe_ok = 1;
    active = 2; edited = 0; bb_sound_pick.cursor = 2; bb_patch_index[0] = 2;
    for (unsigned p = 0; p < 8; p++) param[p] = (uint16_t)(20 + p);
}
/* PRESETS: one detent (as the firmware's TO_KIT branch: the picker moves, then bb_aud_detent) */
static void detent(int d) { bb_sound_pick.cursor = (uint8_t)((int)bb_sound_pick.cursor + d); bb_aud_detent(); }
/* one main-loop scan: actions (KEEP / HOME), then bb_aud_poll; the owner drains between scans when asked */
static void scan(int keep, int home, int drain) { fm1_ms += 10; if (home) bb_audition_home(&bb_aud); if (keep) bb_aud_keep(); bb_aud_poll(); if (drain) owner_drain(); }
static void k1_refused(void)
{
    world(); detent(1); CHECK(bb_aud.armed && bb_sound_pick.cursor == 3, "k1 setup");
    room = 0; scan(1, 0, 1);
    CHECK(!says_kept && active == 2, "k1 refused: no SOUND KEPT, still patch 2 (kept %u, active %d)", says_kept, active);
    CHECK(bb_aud.keep && bb_aud.armed && bb_aud.saved, "k1 refused: KEEP owed, audition kept (keep %u armed %u saved %u)",
          bb_aud.keep, bb_aud.armed, bb_aud.saved);
    for (int i = 0; i < 60; i++) scan(0, 0, 1);   /* 600 ms of a full queue */
    CHECK(!says_kept && active == 2 && bb_aud.keep && cmds == 0, "k1 600 ms full: still owed, nothing claimed");
    room = 4; scan(0, 0, 0);
    CHECK(cmds == 1 && bb_sound_pick.pending && !says_kept, "k1 room: the patch is requested, not yet claimed");
    owner_drain(); scan(0, 0, 0);
    CHECK(active == 3 && says_kept == 1 && !bb_aud.keep && !bb_aud.armed && !bb_aud.saved, "k1: patch 3 plays, then SOUND KEPT");
    for (int i = 0; i < 30; i++) scan(0, 0, 1);
    CHECK(says_kept == 1 && cmds == 1, "k1: once");
}
static void k2_pending(void)
{
    world(); detent(1);
    requested = 5; bb_sound_pick.pending = 1;   /* another picker request is in flight (patch 5) */
    scan(1, 0, 0);
    CHECK(!says_kept && bb_aud.keep && bb_aud.armed && cmds == 0, "k2 pending: owed, no attempt, nothing claimed");
    for (int i = 0; i < 10; i++) scan(0, 0, 0);
    CHECK(!says_kept && bb_aud.keep && !busy, "k2 still pending: owed, no commit attempted (%u)", busy);
    owner_drain(); CHECK(active == 5, "k2 the other request applied");
    scan(0, 0, 0); CHECK(cmds == 1 && bb_sound_pick.pending && !says_kept, "k2 resolved: the cursor's patch is requested");
    for (int i = 0; i < 10; i++) scan(0, 0, 0);   /* the patch request in flight: no further attempt */
    CHECK(!busy && !says_kept, "k2 own request in flight: no attempt (%u)", busy);
    owner_drain(); scan(0, 0, 0);
    CHECK(active == 3 && says_kept == 1 && !bb_aud.keep && !bb_aud.saved, "k2: patch 3 plays, then SOUND KEPT (active %d)", active);
}
static void k3_plain(void)
{
    world(); detent(1); detent(1);
    scan(1, 0, 0); CHECK(cmds == 1 && !says_kept && bb_aud.keep == 2, "k3 KEEP loads in the same scan, not yet claimed");
    owner_drain(); scan(0, 0, 0); CHECK(active == 4 && says_kept == 1 && !bb_aud.keep && !bb_aud.saved, "k3 SOUND KEPT once played");
    for (int i = 0; i < 40; i++) scan(0, 0, 1);
    CHECK(says_kept == 1 && cmds == 1, "k3 nothing more");
}
static void k4_nothing_owed(void)
{
    world(); edited = 1;                          /* an edited sound, no audition: KEEP never reloads it */
    scan(1, 0, 1); CHECK(says_kept == 1 && cmds == 0 && edited, "k4 nothing owed: SOUND KEPT at once, no command");
    world(); detent(1); for (int i = 0; i < 40; i++) scan(0, 0, 1);   /* settled and played already */
    CHECK(active == 3 && !bb_aud.armed, "k4 settled");
    scan(1, 0, 1); CHECK(says_kept == 1 && cmds == 1, "k4 after a settle: SOUND KEPT at once");
}
static void k5_dropped(void)
{
    world(); detent(1); room = 0; scan(1, 0, 1); CHECK(bb_aud.keep, "k5 owed");
    room = 10; scan(0, 1, 1); for (int i = 0; i < 20; i++) scan(0, 0, 1);
    CHECK(!says_kept && says_restored == 1 && active == 2 && !bb_aud.keep, "k5 HOME: restored, never SOUND KEPT");
    world(); detent(1); room = 0; scan(1, 0, 1);
    bb_nav.page = BB_NAV_FX; room = 10; for (int i = 0; i < 20; i++) scan(0, 0, 1);
    CHECK(!says_kept && !bb_aud.keep && cmds == 0, "k5 left the page: dropped, nothing claimed");
}
static void k6_owner_drops(void)
{
    world(); detent(1); scan(1, 0, 0); CHECK(bb_sound_pick.pending, "k6 requested");
    bb_sound_pick.pending = 0;                    /* the owner dropped it: still patch 2 */
    scan(0, 0, 0); CHECK(!says_kept && cmds == 2 && bb_sound_pick.pending, "k6 loads again, nothing claimed");
    owner_drain(); scan(0, 0, 0); CHECK(active == 3 && says_kept == 1, "k6 then SOUND KEPT");
    world(); detent(1); observe_ok = 0; scan(1, 0, 1); for (int i = 0; i < 10; i++) scan(0, 0, 1);
    CHECK(!says_kept && bb_aud.keep && bb_aud.armed, "k6 picker not observable: owed, nothing claimed");
    observe_ok = 1; scan(0, 0, 1); scan(0, 0, 1); CHECK(active == 3 && says_kept == 1, "k6 observable again: kept");
}
static void k7_duke(void)
{
    world(); detent(1); fm1_ms += 400; bb_aud_poll();             /* the settle is accepted: pending, armed 0 */
    CHECK(bb_sound_pick.pending && !bb_aud.armed && bb_sound_pick.cursor == 3 && active == 2, "k7 settle in flight");
    scan(1, 0, 0); CHECK(bb_aud.keep && !says_kept, "k7 KEEP owed while the settle is in flight");
    owner_drop(); scan(0, 0, 0);
    CHECK(!says_kept && active == 2, "k7 owner dropped it: no SOUND KEPT (kept %u, active %d)", says_kept, active);
    CHECK(bb_aud.keep && cmds == 2 && bb_sound_pick.pending, "k7 KEEP loads the cursor's patch again (cmds %u)", cmds);
    owner_drain(); scan(0, 0, 0); CHECK(active == 3 && says_kept == 1 && !bb_aud.keep, "k7 then SOUND KEPT");
}
static int cursor_plays(void) { return active == bb_sound_pick.cursor || (edited && bb_patch_index[0] == bb_sound_pick.cursor); }
static void k8_cross(void)
{
    static const char *const EN[6] = {"nothing owed", "stale cursor", "settle armed", "settle in flight", "other request", "refused"};
    static const char *const OU[3] = {"applies", "drops", "plays another"};
    for (unsigned e = 0; e < 6; e++)
        for (unsigned oc = 0; oc < 3; oc++)
            for (unsigned late = 0; late < 4; late++) {      /* the bad outcome lasts 0..3 owner turns, then it applies */
                world();
                switch (e) {
                case 0: break;
                case 1: detent(1); fm1_ms += 400; bb_aud_poll(); owner_drop(); break;   /* cursor 3, still patch 2 */
                case 2: detent(1); break;
                case 3: detent(1); fm1_ms += 400; bb_aud_poll(); break;
                case 4: detent(1); requested = 6; bb_sound_pick.pending = 1; break;
                case 5: detent(1); room = 0; break;
                }
                const int want = bb_sound_pick.cursor;
                unsigned kept0 = says_kept, turns = 0;
                scan(1, 0, 0);
                for (unsigned i = 0; i < 40; i++) {
                    int before = (int)says_kept;
                    if (says_kept > kept0 && i == 0) CHECK(cursor_plays(), "k8 %s x %s: claimed at once without the patch playing", EN[e], OU[oc]);
                    if (bb_sound_pick.pending) {                 /* the owner's turn */
                        if (turns++ < late && oc == 1) owner_drop();
                        else if (turns <= late && oc == 2) { requested = 7; owner_drain(); }
                        else owner_drain();
                    }
                    if (i == 5) room = 10;
                    scan(0, 0, 0);
                    if ((int)says_kept > before) CHECK(cursor_plays(), "k8 %s x %s (%u): SOUND KEPT while patch %d plays, cursor %d",
                                                       EN[e], OU[oc], late, active, bb_sound_pick.cursor);
                }
                CHECK(says_kept - kept0 == 1 && cursor_plays() && active == want && !bb_aud.keep,
                      "k8 %s x %s (%u): kept once with the cursor's patch (kept %u, active %d, want %d)", EN[e], OU[oc], late,
                      says_kept - kept0, active, want);
            }
}
static void k9_edited(void)
{
    world(); edited = 1;                                            /* patch 2, edited (the owner reads it as no patch) */
    detent(1); detent(-1); for (int i = 0; i < 60; i++) scan(0, 0, 1);
    CHECK(cmds == 0 && edited, "k9 back on the edited patch: never reloaded (cmds %u)", cmds);
    scan(1, 0, 1); CHECK(says_kept == 1 && cmds == 0 && edited, "k9 KEEP keeps the edit");
}
int main(void)
{
    k1_refused(); k2_pending(); k3_plain(); k4_nothing_owed(); k5_dropped(); k6_owner_drops(); k7_duke(); k8_cross(); k9_edited();
    printf("dillo keep: %lu checks, %lu failures\n", checks, failures);
    return failures != 0;
}
