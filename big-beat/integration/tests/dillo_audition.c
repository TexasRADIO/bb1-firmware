/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P2: live Dillo patch audition (SPEC 3, acceptance 8.1.9 and 8.1.10).
 * Part 1 drives the real bb_nav.c: keys stay notes on the Dillo PATCH page, held notes survive PLAY / PATCH / SOUND,
 * other pages still release them, EDIT = KEEP stays on the page, HOME restores, no forced PATTERN.
 * Part 2 drives the decisions the firmware uses (bb_dillo_audition.h: settle, restore point, restore) through a
 * scan loop shaped like bb_nav_scan_mode + bb_aud_poll, with a model command queue (capacity, refusals) and a model
 * picker (cursor, active patch, pending request). The model replaces only the audio owner and the picker. */
#include "bb_nav.h"
#include "bb_dillo_audition.h"
#include <stdio.h>
#include <string.h>
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define B(x) (1u << BB_NAV_B_##x)
#ifndef SETTLE_UNDER_TEST
#define SETTLE_UNDER_TEST BB_AUD_SETTLE_MS
#endif
static uint32_t now = 1000;
static bb_nav_facts facts;
static bb_nav_out o;
static void facts_reset(void)
{
    memset(&facts, 0, sizeof facts);
    facts.kit_slot = facts.kit_slot_queued = facts.section_queued = facts.section = BB_NAV_NONE_IDX;
    facts.list_cursor = facts.list_playing = facts.list_queued = BB_NAV_NONE_IDX;
    facts.list_count = 9;
}
static void scan_at(bb_nav_state *s, uint32_t pressed, uint32_t held, uint32_t keys, uint32_t keys_held, int16_t preset,
                    uint32_t dt)
{
    bb_nav_in in; memset(&in, 0, sizeof in);
    in.pressed = pressed; in.held = held; in.keys = keys; in.keys_held = keys_held; in.now_ms = (now += dt);
    in.facts = facts; in.enc[BB_NAV_E_PRESET] = preset;
    bb_nav_input(s, &in, &o);
}
static void dillo(bb_nav_state *s)
{
    bb_nav_init(s, BB_NAV_STUDIO, 600);
    bb_nav_set_features(s, BB_NAV_F_SCALE | BB_NAV_F_STEP_PITCH);
    bb_nav_set_instrument(s, BB_NAV_INSTR_DILLO, 1);
    bb_nav_set_focus(s, 2);
    s->page = BB_NAV_PLAY; s->play_keys = BB_NAV_PLAY_LIVE;
}
static unsigned releases(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < o.kev_count; i++) n += (o.kev[i].flags & BB_NAV_KF_RELEASE) != 0;
    return n;
}
/* ------------------------------------------------------------------------------------------------ part 1: bb_nav */
static void part_nav(void)
{
    bb_nav_state s; const uint32_t K = 1u << 3, K2 = 1u << 20;   /* white 4 and black 5 */
    uint8_t arg0, arg;
    /* hold a note on PLAY, turn PRESETS 3 detents: PATCH, the note never released, keys are notes */
    dillo(&s); facts_reset();
    scan_at(&s, 0, 0, K, K, 0, 10);
    CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_NOTE && !(o.kev[0].flags & BB_NAV_KF_RELEASE), "note on");
    arg0 = o.kev[0].arg;
    for (int d = 0; d < 3; d++) {
        scan_at(&s, 0, 0, 0, K, 1, 100);
        CHECK(s.page == BB_NAV_KIT, "PRESETS opens PATCH (detent %d)", d);
        CHECK(releases() == 0, "held note survives detent %d (%u releases)", d, releases());
        CHECK(o.enc[BB_NAV_E_PRESET].target == BB_NAV_TO_KIT && o.enc[BB_NAV_E_PRESET].delta == 1, "detent routed");
    }
    CHECK(bb_nav_keys_for(&s) == BB_NAV_KEYS_NOTES, "PATCH keys are notes");
    for (uint8_t k = 0; k < BB_NAV_KEYS; k++) {
        bb_nav_state p = s; p.page = BB_NAV_PLAY; uint8_t a2;
        uint8_t r1 = bb_nav_key_role(&s, k, 0, &arg), r2 = bb_nav_key_role(&p, k, 0, &a2);
        CHECK(r1 == r2 && arg == a2, "key %u: PATCH role %u/%u = PLAY %u/%u", k, r1, arg, r2, a2);
    }
    CHECK(!strcmp(bb_nav_edit_label(&s, &facts), "KEEP"), "EDIT caption KEEP (%s)", bb_nav_edit_label(&s, &facts));
    /* a second note on PATCH plays */
    scan_at(&s, 0, 0, K2, K | K2, 0, 10);
    CHECK(o.kev_count == 1 && o.kev[0].role == BB_NAV_K_NOTE && !(o.kev[0].flags & BB_NAV_KF_RELEASE), "note on PATCH");
    /* EDIT = KEEP: the confirm action, page stays (the firmware does not call bb_nav_kit_confirmed for Dillo) */
    scan_at(&s, B(EDIT), B(EDIT), 0, K | K2, 0, 10); scan_at(&s, 0, 0, 0, K | K2, 0, 10);
    CHECK(s.page == BB_NAV_KIT && releases() == 0, "KEEP stays, notes held");
    /* PATCH -> SOUND (ENV) -> PLAY (ARP) -> PATCH: zero releases */
    scan_at(&s, B(ENV), B(ENV), 0, K | K2, 0, 10); CHECK(s.page == BB_NAV_SOUND && releases() == 0, "to SOUND: 0 releases");
    scan_at(&s, 0, 0, 0, K | K2, 0, 10);
    scan_at(&s, B(ARP), B(ARP), 0, K | K2, 0, 10); CHECK(s.page == BB_NAV_PLAY && releases() == 0, "to PLAY: 0 releases");
    scan_at(&s, 0, 0, 0, K | K2, 0, 10);
    scan_at(&s, 0, 0, 0, K | K2, -1, 10); CHECK(s.page == BB_NAV_KIT && releases() == 0, "to PATCH: 0 releases");
    /* lift the first key: one release, the original pitch */
    scan_at(&s, 0, 0, 0, K2, 0, 10);
    CHECK(o.kev_count == 1 && (o.kev[0].flags & BB_NAV_KF_RELEASE) && o.kev[0].arg == arg0 && o.kev[0].key == 3,
          "lifted: one release of the held pitch");
    /* PATCH -> FX: one release each (keys not notes there) */
    scan_at(&s, B(FX), B(FX), 0, K2, 0, 10);
    CHECK(s.page == BB_NAV_FX && releases() == 1, "to FX: the held note released (%u)", releases());
    /* HOME tap on PATCH: RESTORE; on drum KIT: none; a HOME hold (help) on PATCH: none */
    dillo(&s); scan_at(&s, 0, 0, 0, 0, 1, 10); CHECK(s.page == BB_NAV_KIT, "PATCH");
    scan_at(&s, B(HOME), B(HOME), 0, 0, 0, 10); scan_at(&s, 0, 0, 0, 0, 0, 10);
    CHECK((o.actions & BB_NAV_A_RESTORE) && s.page != BB_NAV_KIT && s.page != BB_NAV_PATTERN, "HOME tap restores");
    dillo(&s); scan_at(&s, 0, 0, 0, 0, 1, 10);
    scan_at(&s, B(HOME), B(HOME), 0, 0, 0, 10); scan_at(&s, 0, B(HOME), 0, 0, 0, 700); CHECK(s.help, "help");
    scan_at(&s, 0, 0, 0, 0, 0, 10); CHECK(!(o.actions & BB_NAV_A_RESTORE) && s.page == BB_NAV_KIT, "help: no restore");
    dillo(&s); bb_nav_set_instrument(&s, BB_NAV_INSTR_DRUM, 2); bb_nav_set_focus(&s, 0);
    scan_at(&s, 0, 0, 0, 0, 1, 10); scan_at(&s, B(HOME), B(HOME), 0, 0, 0, 10); scan_at(&s, 0, 0, 0, 0, 0, 10);
    CHECK(!(o.actions & BB_NAV_A_RESTORE), "drum KIT: no restore");
    /* panic (REC+HOME) still releases every held note */
    dillo(&s); scan_at(&s, 0, 0, K | K2, K | K2, 0, 10); scan_at(&s, 0, 0, 0, K | K2, 1, 10);
    scan_at(&s, B(HOME), B(HOME) | B(REC), 0, K | K2, 0, 10);
    CHECK((o.actions & BB_NAV_A_PANIC) && releases() == 2, "panic releases both (%u)", releases());
    /* focus change to a drum releases (role changes) */
    dillo(&s); scan_at(&s, 0, 0, K, K, 0, 10); bb_nav_set_instrument(&s, BB_NAV_INSTR_DRUM, 2); bb_nav_set_focus(&s, 0);
    bb_nav_goto(&s, BB_NAV_PLAY, &o); CHECK(releases() == 1, "to a drum layer: released");
    /* never PATTERN from the Dillo path */
    dillo(&s); for (int i = 0; i < 20; i++) { scan_at(&s, i % 5 ? 0 : B(EDIT), 0, 0, 0, (int16_t)(i & 1 ? 1 : -1), 50);
        CHECK(s.page != BB_NAV_PATTERN, "never PATTERN"); }
}
/* ---------------------------------------------------- part 2: settle / restore through the firmware's scan shape */
static bb_audition aud;
static unsigned queue_room, cmds_patch, cmds_param, cursor, active, pending, restored, tried_pending;
static uint16_t param[BB_AUD_PARAMS], bank[9][BB_AUD_PARAMS];
static unsigned get(unsigned p) { return param[p]; }
static int send(int patch, unsigned value, unsigned p)
{
    if (!queue_room) return 0;
    queue_room--;
    if (patch) { cmds_patch++; active = value; memcpy(param, bank[value], sizeof param); }
    else { cmds_param++; param[p] = (uint16_t)value; }
    return 1;
}
/* bb_aud_load: load the cursor's patch unless it is the playing one; a refused push stays armed */
static void load(void)
{
    if (active == cursor) { aud.armed = 0; return; }
    if (pending) { tried_pending++; return; }   /* the picker would refuse BUSY; the audition must not even try */
    if (!queue_room) return;
    queue_room--; cmds_patch++; pending = 1; aud.armed = 0;
}
static int mload(void) { unsigned c = cmds_patch; load(); return cmds_patch != c || active == cursor; }   /* 1 = accepted */
static unsigned kept;
static void owner_tick(void) { if (pending) { pending = 0; active = cursor; memcpy(param, bank[active], sizeof param); } }
/* one main-loop scan: nav input, the TO_KIT detent, HOME/KEEP actions, then bb_aud_poll */
static void fw_scan(bb_nav_state *s, uint32_t pressed, uint32_t held, int16_t preset, uint32_t dt)
{
    scan_at(s, pressed, held, 0, 0, preset, dt);
    if (o.actions & BB_NAV_A_RESTORE) bb_audition_home(&aud);
    if ((o.actions & BB_NAV_A_KIT_CONFIRM)) bb_audition_keep(&aud);   /* bb_aud_keep (RC2: always owed, then observed) */
    const int page = s->page == BB_NAV_KIT && s->instrument != BB_NAV_INSTR_DRUM;
    if (o.enc[BB_NAV_E_PRESET].target == BB_NAV_TO_KIT && page && !pending) {
        int c = (int)cursor + o.enc[BB_NAV_E_PRESET].delta; cursor = (unsigned)(c < 0 ? 0 : c > 8 ? 8 : c);
        if (bb_audition_detent(&aud, now)) {   /* as bb_aud_detent */
            aud.index = (uint8_t)active; for (unsigned p = 0; p < BB_AUD_PARAMS; p++) aud.value[p] = (uint16_t)get(p);
        }
    }
    if (aud.restore) { if (bb_audition_restore(&aud, get, send)) restored++; return; }
    if (!page) { bb_audition_leave(&aud); return; }
    if (aud.keep) { kept += (unsigned)bb_audition_keep_step(&aud, (int)pending, !pending && active == cursor, mload); return; }
    if (bb_audition_due(&aud, now, 1, (int)pending)) load();
}
static void world(bb_nav_state *s)
{
    dillo(s); facts_reset(); memset(&aud, 0, sizeof aud);
    queue_room = 1000; tried_pending = 0; kept = 0; cmds_patch = cmds_param = 0; cursor = active = 2; pending = 0; restored = 0;
    for (unsigned b = 0; b < 9; b++) for (unsigned p = 0; p < BB_AUD_PARAMS; p++) bank[b][p] = (uint16_t)(b * 10 + p);
    memcpy(param, bank[2], sizeof param);
}
static void part_settle(void)
{
    bb_nav_state s;
    /* 3 detents, then 349 ms: nothing; 351: one */
    world(&s);
    for (int d = 0; d < 3; d++) fw_scan(&s, 0, 0, 1, 40);
    uint32_t last = now;
    while (now - last < SETTLE_UNDER_TEST - 1u) fw_scan(&s, 0, 0, 0, 1);
    CHECK(now - last == 349 && cmds_patch == 0, "349 ms: no command (%u at %u ms)", cmds_patch, now - last);
    fw_scan(&s, 0, 0, 0, 2);
    CHECK(now - last == 351 && cmds_patch == 1 && cursor == 5, "351 ms: one command for patch 5 (%u)", cmds_patch);
    owner_tick(); for (int i = 0; i < 100; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 1 && active == 5, "one per settle");
    /* 9 detents at 150 ms: one command, at the end */
    world(&s);
    for (int d = 0; d < 9; d++) { fw_scan(&s, 0, 0, d < 6 ? 1 : -1, d ? 150 : 10); CHECK(cmds_patch == 0, "passing %d", d); }
    for (int i = 0; i < 40; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 1 && cursor == 5, "9 detents at 150 ms: one command (%u) for %u", cmds_patch, cursor);
    /* back to the playing patch: nothing loads (an edited sound is not reloaded) */
    world(&s); param[0] = 999;
    fw_scan(&s, 0, 0, 1, 10); fw_scan(&s, 0, 0, -1, 100); for (int i = 0; i < 60; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 0 && param[0] == 999, "back on the playing patch: no load");
    /* pending request: no command until resolved, then the settle-load */
    world(&s); pending = 1;
    fw_scan(&s, 0, 0, 1, 10); for (int i = 0; i < 60; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 0 && tried_pending == 0, "pending: blocked, no attempt (%u)", tried_pending);
    /* a request that becomes pending after the detent (e.g. the previous settle-load in flight): waits, then loads */
    world(&s); fw_scan(&s, 0, 0, 1, 10); pending = 1; for (int i = 0; i < 60; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 0 && tried_pending == 0 && aud.armed, "pending after the detent: waits, no attempt (%u)", tried_pending);
    pending = 0; fw_scan(&s, 0, 0, 0, 10);   /* the other request settled; cursor 3 is not playing */
    CHECK(cmds_patch == 1, "resolved: the settle-load goes (%u)", cmds_patch);
    /* queue full: kept, retried on the next scans, latest wins */
    world(&s); queue_room = 0;
    fw_scan(&s, 0, 0, 1, 10); fw_scan(&s, 0, 0, 1, 10); for (int i = 0; i < 60; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 0 && aud.armed, "queue full: still owed");
    queue_room = 5; fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 1 && cursor == 4, "queue free: the latest patch loads");
    /* KEEP before the settle: loads now, stays; SOUND KEPT (and the restore point dropped) once the owner plays it */
    world(&s); fw_scan(&s, 0, 0, 1, 10); fw_scan(&s, B(EDIT), B(EDIT), 0, 10); fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 1 && s.page == BB_NAV_KIT && aud.saved && !kept, "KEEP loads now and stays, not yet kept");
    owner_tick(); fw_scan(&s, 0, 0, 0, 10);
    CHECK(kept == 1 && !aud.saved && active == cursor, "KEEP: kept once the patch plays");
    for (int i = 0; i < 60; i++) fw_scan(&s, 0, 0, 0, 10);
    CHECK(cmds_patch == 1, "KEEP: nothing more");
}
static void part_restore(void)
{
    bb_nav_state s;
    /* edited sound, audition two patches, HOME: patch + the differing parameters, values equal the restore point */
    for (unsigned room = 1; room <= 10; room++) {
        world(&s); param[1] = 777; param[6] = 1;   /* edits on patch 2 */
        uint16_t before[BB_AUD_PARAMS]; memcpy(before, param, sizeof param);
        fw_scan(&s, 0, 0, 1, 10); for (int i = 0; i < 40; i++) fw_scan(&s, 0, 0, 0, 10); owner_tick();
        fw_scan(&s, 0, 0, 1, 10); for (int i = 0; i < 40; i++) fw_scan(&s, 0, 0, 0, 10); owner_tick();
        CHECK(active == 4 && cmds_patch == 2, "auditioned 3 then 4");
        cmds_patch = cmds_param = 0; queue_room = room;
        fw_scan(&s, B(HOME), B(HOME), 0, 10); fw_scan(&s, 0, 0, 0, 10);
        for (int i = 0; i < 20 && aud.restore; i++) { queue_room += room; fw_scan(&s, 0, 0, 0, 10); }
        CHECK(restored == 1 && !aud.restore, "restored (room %u)", room);
        CHECK(!memcmp(param, before, sizeof param) && active == 2, "sound equals the restore point (room %u)", room);
        CHECK(cmds_patch == 1 && cmds_param == 2, "one patch + 2 parameters (%u, %u)", cmds_patch, cmds_param);
    }
    /* leaving another way keeps the new sound; a later HOME on another page restores nothing */
    world(&s); fw_scan(&s, 0, 0, 1, 10); for (int i = 0; i < 40; i++) fw_scan(&s, 0, 0, 0, 10); owner_tick();
    fw_scan(&s, B(FX), B(FX), 0, 10); fw_scan(&s, 0, 0, 0, 10);
    fw_scan(&s, B(HOME), B(HOME), 0, 10); fw_scan(&s, 0, 0, 0, 10);
    CHECK(active == 3 && !restored && !aud.saved, "left by FX: new sound kept");
    /* the restore point is taken once per audition (first detent), not per detent */
    world(&s); fw_scan(&s, 0, 0, 1, 10); param[0] = 4242; fw_scan(&s, 0, 0, 1, 10);
    CHECK(aud.value[0] == bank[2][0], "restore point from the first detent");
}
int main(void)
{
    part_nav();
    part_settle();
    part_restore();
    printf("dillo audition: %lu checks, %lu failures\n", checks, failures);
    return failures != 0;
}
