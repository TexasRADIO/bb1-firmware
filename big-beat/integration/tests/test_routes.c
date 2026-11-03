/* SPDX-License-Identifier: GPL-3.0-only
 * Route matrix + reachability: walk every navigation state reachable from boot
 * with the real navigation model (bb_nav.c from the candidate source), using
 * every button alone, REC+button, a HOME hold (HELP), and SELECT/ALGORITHM/
 * PRESETS turns (with the firmware's top-level CONDUCTOR / BIG BEAT / DILLO
 * destinations and local player feedback emulated). For each state: the v3 page bb_v3_route() picks,
 * the four knob labels, and the HOME exit. Fails if any normal state routes
 * to a legacy renderer (BB_UI_NO), if HOME does not return to STUDIO, or if a
 * knob label names a machine this product does not have.
 * Writes the route matrix as CSV to argv[1]. */
#include "bb_ui_v3_route.h"
#include "bb/bb_top_mode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t now_ms = 1000;
static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static const char *const PAGE[] = {"STUDIO", "PLAY", "KIT", "PATTERN", "SOUND", "FX", "SAVE", "EVOLVE", "GLOBAL"};
static const char *const UIP[] = {"STUDIO(HOME)", "PLAY_DRUMS", "STEPS", "PLAY_DILLO", "KIT", "PATTERN", "FX", "KNOBS", "ATTRACT"};
static const char *const BTN[] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ROW[] = {"DRUM A", "DRUM B", "DILLO"};

typedef struct {
    bb_nav_state s;
    uint8_t row, mode, conductor_row, big_beat_row;
    char path[160];
} node;
#define MAXN 4096
static node q[MAXN]; static unsigned nq;

static void feedback(bb_nav_state *s, uint8_t row) {
    bb_nav_set_instrument(s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, 1);
    bb_nav_set_focus(s, row);
}
static void scan(node *n, uint32_t pressed, uint32_t held, int enc, int16_t d) {
    bb_nav_state *s = &n->s;
    bb_nav_in in; bb_nav_out o;
    memset(&in, 0, sizeof in); memset(&o, 0, sizeof o);
    in.pressed = pressed; in.held = held; in.now_ms = now_ms; now_ms += 10;
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    in.facts.section_count = 4; in.facts.list_count = 16; in.facts.step_banks = 2;
    if (enc >= 0) in.enc[enc] = d;
    bb_nav_input(s, &in, &o);
    if (enc == BB_NAV_E_SELECT && o.enc[BB_NAV_E_SELECT].target == BB_NAV_TO_INSTRUMENT) {
        if (s->landing) {
            unsigned next = bb_top_mode_step(n->mode, o.enc[BB_NAV_E_SELECT].delta);
            if (next != n->mode) {
                bb_nav_out aux;
                if (n->mode == BB_TOP_CONDUCTOR && n->row < 3u) n->conductor_row = n->row;
                if (n->mode == BB_TOP_BIG_BEAT && n->row < 2u) n->big_beat_row = n->row;
                n->mode = (uint8_t)next;
                n->row = (uint8_t)bb_top_mode_row(next, n->conductor_row, n->big_beat_row, 2u);
                s->home_page = (uint8_t)bb_top_mode_home(next, BB_NAV_STUDIO, BB_NAV_PLAY);
                s->play_keys = BB_NAV_PLAY_LIVE;
                feedback(s, n->row);
                bb_nav_goto(s, s->home_page, &aux);
                s->landing = 1;
            }
        } else {
            int next = (int)n->row + (o.enc[BB_NAV_E_SELECT].delta > 0 ? 1 : -1);
            n->row = (uint8_t)(next < 0 ? 0 : next > 2 ? 2 : next);
            feedback(s, n->row);
        }
    }
    if (s->landing) {
        n->row = (uint8_t)bb_top_mode_row(n->mode, n->conductor_row, n->big_beat_row, 2u);
        feedback(s, n->row);
    }
}
static int key_of(const node *n) {   /* the states that change what is drawn */
    const bb_nav_state *s = &n->s;
    return s->page | s->bank << 4 | s->play_keys << 8 | n->row << 9 | s->help << 11 |
           n->mode << 12 | s->landing << 14;
}
static int seen[1 << 15];
static void push(const node *n, const char *step) {
    int k = key_of(n);
    if (seen[k] || nq >= MAXN) return;
    seen[k] = 1; q[nq] = *n;
    snprintf(q[nq].path, sizeof q[nq].path, "%s%s%s", n->path, n->path[0] ? " > " : "", step);
    nq++;
}

/* A destination change must drain a live Dillo note even though the physical
 * key is still held.  The firmware sends these events before applying the new
 * page, so a Dillo-to-Big-Beat turn cannot leave a hidden bass note sounding. */
static void check_destination_note_release(void) {
    node n; bb_nav_in in; bb_nav_out press, release;
    memset(&n, 0, sizeof n); memset(&in, 0, sizeof in);
    bb_nav_init(&n.s, BB_NAV_PLAY, 600);
    n.mode = BB_TOP_DILLO; n.row = 2; feedback(&n.s, n.row);
    in.keys = in.keys_held = 1u; in.now_ms = now_ms; now_ms += 10;
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    bb_nav_input(&n.s, &in, &press);
    CHECK(press.kev_count == 1 && press.kev[0].role == BB_NAV_K_NOTE &&
          !(press.kev[0].flags & BB_NAV_KF_RELEASE), "Dillo live key did not emit note-on");
    CHECK(n.s.keys_active == 1u, "Dillo live key was not retained as active");

    n.mode = BB_TOP_BIG_BEAT; n.row = 0; feedback(&n.s, n.row);
    bb_nav_goto(&n.s, BB_NAV_PLAY, &release);
    n.s.landing = 1; /* bb_nav_mode_move sets this after its forced page drain */
    CHECK(release.kev_count == 1 && release.kev[0].role == BB_NAV_K_NOTE &&
          (release.kev[0].flags & BB_NAV_KF_RELEASE), "destination change did not emit Dillo note-off");
    CHECK(!n.s.keys_active && n.s.page == BB_NAV_PLAY && n.s.landing,
          "destination change did not finish on clean Big Beat landing");
}

int main(int argc, char **argv) {
    FILE *csv = argc > 1 ? fopen(argv[1], "w") : NULL;
    node start; memset(&start, 0, sizeof start);
    bb_nav_init(&start.s, BB_NAV_STUDIO, 600); start.row = 0; start.mode = BB_TOP_CONDUCTOR; feedback(&start.s, 0);
    check_destination_note_release();
    push(&start, "boot");
    for (unsigned i = 0; i < nq; i++) {
        for (unsigned b = 0; b < BB_NAV_BUTTONS; b++) {
            if (b == BB_NAV_B_PLAY || b == BB_NAV_B_REC) continue;           /* transport / modifier */
            if (b == BB_NAV_B_OCTDN || b == BB_NAV_B_OCTUP) {               /* single OCT: in-page */
                node n = q[i];
                scan(&n, BB_NAV_BIT(b), BB_NAV_BIT(b), -1, 0); scan(&n, 0, 0, -1, 0);
                push(&n, BTN[b]); continue;
            }
            for (int rec = 0; rec < 2; rec++) {
                node n = q[i]; uint32_t mod = rec ? BB_NAV_BIT(BB_NAV_B_REC) : 0;
                scan(&n, BB_NAV_BIT(b), BB_NAV_BIT(b) | mod, -1, 0);
                scan(&n, 0, mod, -1, 0);
                if (rec) scan(&n, 0, 0, -1, 0);
                char step[24]; snprintf(step, sizeof step, "%s%s", rec ? "REC+" : "", BTN[b]);
                push(&n, step);
            }
        }
        { /* hold HOME past the hold time: HELP overlay */
            node n = q[i];
            scan(&n, BB_NAV_BIT(BB_NAV_B_HOME), BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
            for (int t = 0; t < 80; t++) scan(&n, 0, BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
            push(&n, "hold HOME");
        }
        for (int e = 0; e < 3; e++) for (int d = -1; d <= 1; d += 2) {
            node n = q[i];
            scan(&n, 0, 0, e, (int16_t)d);
            push(&n, e == 0 ? (d > 0 ? "SELECT+" : "SELECT-") : e == 1 ? (d > 0 ? "ALGO+" : "ALGO-") : (d > 0 ? "PRESETS+" : "PRESETS-"));
        }
    }
    if (csv) fprintf(csv, "nav_page,bank,top_mode,instrument,play_keys,help,v3_page,page_name,K1,K2,K3,K4,home_exit,shortest_button_path\n");
    unsigned per_page[9] = {0}, legacy = 0;
    for (unsigned i = 0; i < nq; i++) {
        node *n = &q[i]; bb_nav_state *s = &n->s;
        uint8_t page = bb_v3_route(s, 0);
        uint8_t roles[4]; const char *lab[4];
        bb_nav_knobs(s, roles, lab);
        CHECK(page != BB_UI_NO, "%s routes to legacy (%s)", PAGE[s->page], n->path);
        if (page == BB_UI_NO) legacy++;
        if (s->page < 9) per_page[s->page]++;
        for (int k = 0; k < 4; k++)
            CHECK(!strstr(lab[k], "KIT C") && !strstr(lab[k], "KIT D") && !strstr(lab[k], "TELEGR"),
                  "unavailable machine in knob label '%s' (%s)", lab[k], n->path);
        /* HOME exit: a plain HOME tap from here lands on STUDIO */
        node h = *n; const char *exit_page;
        if (h.s.help) { scan(&h, 0, 0, -1, 0); }                 /* let go of HOME first */
        scan(&h, BB_NAV_BIT(BB_NAV_B_HOME), BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
        scan(&h, 0, 0, -1, 0);
        exit_page = h.s.page < 9 ? PAGE[h.s.page] : "?";
        CHECK(h.s.page == (n->mode == BB_TOP_CONDUCTOR ? BB_NAV_STUDIO : BB_NAV_PLAY) && h.s.landing,
              "HOME from %s lands on %s (%s)", PAGE[s->page], exit_page, n->path);
        if (csv)
            fprintf(csv, "%s,%s,%s,%s,%s,%d,%s,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%s,\"%s\"\n", PAGE[s->page],
                    bb_nav_bank_name(s) ? bb_nav_bank_name(s) : "", bb_top_mode_name(n->mode), ROW[n->row],
                    s->play_keys == BB_NAV_PLAY_STEPS ? "STEPS" : "LIVE", s->help,
                    page == BB_UI_NO ? "LEGACY" : UIP[page], bb_nav_page_name(s), lab[0], lab[1], lab[2], lab[3], exit_page, n->path);
    }
    if (csv) fclose(csv);
    printf("routes: %u reachable states (", nq);
    for (int p = 0; p < 9; p++) printf("%s%s %u", p ? ", " : "", PAGE[p], per_page[p]);
    printf("); legacy normal pages %u; %lu checks, %lu failures\n", legacy, checks, failures);
    return failures != 0;
}
