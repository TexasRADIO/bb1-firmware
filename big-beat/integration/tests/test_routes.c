/* SPDX-License-Identifier: GPL-3.0-only
 * Route matrix + reachability: walk every navigation state reachable from boot
 * with the real navigation model (bb_nav.c from the candidate source), using
 * every button alone, REC+button, a HOME hold (HELP), and SELECT/ALGORITHM/
 * PRESETS turns (with the firmware's instrument feedback emulated: rows
 * DRUM A, DRUM B, DILLO). For each state: the v3 page bb_v3_route() picks,
 * the four knob labels, and the HOME exit. Fails if any normal state routes
 * to a legacy renderer (BB_UI_NO), if HOME does not return to STUDIO, or if a
 * knob label names a machine this product does not have.
 * Writes the route matrix as CSV to argv[1]. */
#include "bb_ui_v3_route.h"
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

typedef struct { bb_nav_state s; uint8_t row; char path[160]; } node;
#define MAXN 4096
static node q[MAXN]; static unsigned nq;

static void feedback(bb_nav_state *s, uint8_t row) {
    bb_nav_set_instrument(s, row < 2 ? BB_NAV_INSTR_DRUM : BB_NAV_INSTR_DILLO, 1);
    bb_nav_set_focus(s, row);
}
static void scan(bb_nav_state *s, uint8_t *row, uint32_t pressed, uint32_t held, int enc, int16_t d) {
    bb_nav_in in; bb_nav_out o;
    memset(&in, 0, sizeof in); memset(&o, 0, sizeof o);
    in.pressed = pressed; in.held = held; in.now_ms = now_ms; now_ms += 10;
    in.facts.kit_slot = in.facts.kit_slot_queued = in.facts.section_queued = BB_NAV_NONE_IDX;
    in.facts.list_cursor = in.facts.list_playing = in.facts.list_queued = BB_NAV_NONE_IDX;
    in.facts.section_count = 4; in.facts.list_count = 16; in.facts.step_banks = 2;
    if (enc >= 0) in.enc[enc] = d;
    bb_nav_input(s, &in, &o);
    if (enc == BB_NAV_E_SELECT && o.enc[BB_NAV_E_SELECT].target == BB_NAV_TO_INSTRUMENT) {
        *row = (uint8_t)((*row + 3 + (o.enc[BB_NAV_E_SELECT].delta > 0 ? 1 : -1)) % 3);
        feedback(s, *row);
    }
}
static int key_of(const node *n) {   /* the states that change what is drawn */
    const bb_nav_state *s = &n->s;
    return s->page | s->bank << 4 | s->play_keys << 8 | n->row << 9 | s->help << 11;
}
static int seen[1 << 12];
static void push(const node *n, const char *step) {
    int k = key_of(n);
    if (seen[k] || nq >= MAXN) return;
    seen[k] = 1; q[nq] = *n;
    snprintf(q[nq].path, sizeof q[nq].path, "%s%s%s", n->path, n->path[0] ? " > " : "", step);
    nq++;
}

int main(int argc, char **argv) {
    FILE *csv = argc > 1 ? fopen(argv[1], "w") : NULL;
    node start; memset(&start, 0, sizeof start);
    bb_nav_init(&start.s, BB_NAV_STUDIO, 600); start.row = 0; feedback(&start.s, 0);
    push(&start, "boot");
    for (unsigned i = 0; i < nq; i++) {
        for (unsigned b = 0; b < BB_NAV_BUTTONS; b++) {
            if (b == BB_NAV_B_PLAY || b == BB_NAV_B_REC) continue;           /* transport / modifier */
            if (b == BB_NAV_B_OCTDN || b == BB_NAV_B_OCTUP) {               /* single OCT: in-page */
                node n = q[i];
                scan(&n.s, &n.row, BB_NAV_BIT(b), BB_NAV_BIT(b), -1, 0); scan(&n.s, &n.row, 0, 0, -1, 0);
                push(&n, BTN[b]); continue;
            }
            for (int rec = 0; rec < 2; rec++) {
                node n = q[i]; uint32_t mod = rec ? BB_NAV_BIT(BB_NAV_B_REC) : 0;
                scan(&n.s, &n.row, BB_NAV_BIT(b), BB_NAV_BIT(b) | mod, -1, 0);
                scan(&n.s, &n.row, 0, mod, -1, 0);
                if (rec) scan(&n.s, &n.row, 0, 0, -1, 0);
                char step[24]; snprintf(step, sizeof step, "%s%s", rec ? "REC+" : "", BTN[b]);
                push(&n, step);
            }
        }
        { /* hold HOME past the hold time: HELP overlay */
            node n = q[i];
            scan(&n.s, &n.row, BB_NAV_BIT(BB_NAV_B_HOME), BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
            for (int t = 0; t < 80; t++) scan(&n.s, &n.row, 0, BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
            push(&n, "hold HOME");
        }
        for (int e = 0; e < 3; e++) for (int d = -1; d <= 1; d += 2) {
            node n = q[i];
            scan(&n.s, &n.row, 0, 0, e, (int16_t)d);
            push(&n, e == 0 ? (d > 0 ? "SELECT+" : "SELECT-") : e == 1 ? (d > 0 ? "ALGO+" : "ALGO-") : (d > 0 ? "PRESETS+" : "PRESETS-"));
        }
    }
    if (csv) fprintf(csv, "nav_page,bank,instrument,play_keys,help,v3_page,page_name,K1,K2,K3,K4,home_exit,shortest_button_path\n");
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
        if (h.s.help) { scan(&h.s, &h.row, 0, 0, -1, 0); }                 /* let go of HOME first */
        scan(&h.s, &h.row, BB_NAV_BIT(BB_NAV_B_HOME), BB_NAV_BIT(BB_NAV_B_HOME), -1, 0);
        scan(&h.s, &h.row, 0, 0, -1, 0);
        exit_page = h.s.page < 9 ? PAGE[h.s.page] : "?";
        CHECK(h.s.page == BB_NAV_STUDIO || (n->s.page == BB_NAV_STUDIO && h.s.page == BB_NAV_PLAY),
              "HOME from %s lands on %s (%s)", PAGE[s->page], exit_page, n->path);
        if (csv)
            fprintf(csv, "%s,%s,%s,%s,%d,%s,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%s,\"%s\"\n", PAGE[s->page],
                    bb_nav_bank_name(s) ? bb_nav_bank_name(s) : "", ROW[n->row],
                    s->play_keys == BB_NAV_PLAY_STEPS ? "STEPS" : "LIVE", s->help,
                    page == BB_UI_NO ? "LEGACY" : UIP[page], bb_nav_page_name(s), lab[0], lab[1], lab[2], lab[3], exit_page, n->path);
    }
    if (csv) fclose(csv);
    printf("routes: %u reachable states (", nq);
    for (int p = 0; p < 9; p++) printf("%s%s %u", p ? ", " : "", PAGE[p], per_page[p]);
    printf("); legacy normal pages %u; %lu checks, %lu failures\n", legacy, checks, failures);
    return failures != 0;
}
