/* SPDX-License-Identifier: GPL-3.0-only
 * 1.5 song tempo gate: on the real firmware catalogue, every song bank reads a tempo of 30..300 BPM and a swing of
 * 100..150 (x2 scale) through bb_factory_read_song_tempo, walking the browser exactly as the player does; eras are
 * refused by the song reader (they keep the recipe path); and the sheet tempos land: Paul Revere 92, Egypt, Egypt 127,
 * Tainted Love 72 with 66% swing (x2 132), 777-9311 247. Host only. */
#include <stdio.h>
#include <string.h>
#include "bb_factory.h"
#include "bb_factory_recipe.h"
static unsigned checks, failures;
static void expect(int ok, const char *what, const char *song) { checks++; if (!ok) { failures++; printf("FAIL %s: %s\n", what, song); } }
int main(void)
{
    if (!bb_factory_init()) { puts("catalogue did not open"); return 2; }
    bb_browser v = bb_factory_browser;
    static const struct { const char *song; unsigned bpm, x2; } want[] = {
        {"Paul Revere", 92, 100}, {"Egypt, Egypt", 127, 100}, {"Tainted Love", 72, 132}, {"777-9311", 247, 100},
        {"Radioactivity", 61, 100}, {"True Faith", 117, 100}};
    unsigned found[6] = {0}, songs = 0;
    expect(bb_browser_group(&v, BB_BROWSER_SONGS), "songs group", "-");
    size_t n = bb_browser_bank_count(&v);
    for (size_t i = 0; i < n; i++) {
        bb_factory_choice ch; char name[64] = {0};
        expect(bb_browser_current(&v, &ch), "current", "-");
        bb_catalogue_copy_text(&bb_factory_catalogue, bb_catalogue_find(&bb_factory_catalogue, ch.bank, "song"), name, sizeof name);
        unsigned bpm = 0, x2 = 0;
        int ok = bb_factory_read_song_tempo(&bb_factory_catalogue, &ch, &bpm, &x2);
        expect(ok && bpm >= 30 && bpm <= 300 && x2 >= 100 && x2 <= 150, "tempo in range", name);
        for (unsigned k = 0; k < 6; k++) if (!strcmp(name, want[k].song)) { found[k] = 1;
            expect(bpm == want[k].bpm && x2 == want[k].x2, "sheet tempo", name); }
        songs++;
        if (i + 1 < n) expect(bb_browser_move_bank(&v, 1), "next song", name);
    }
    for (unsigned k = 0; k < 6; k++) expect(found[k], "song present", want[k].song);
    bb_browser e = bb_factory_browser; bb_factory_choice ec; unsigned b, s;
    expect(bb_browser_group(&e, BB_BROWSER_ERAS) && bb_browser_current(&e, &ec) &&
           !bb_factory_read_song_tempo(&bb_factory_catalogue, &ec, &b, &s), "eras refused by the song reader", "-");
    printf("song tempo: %u songs, %u checks, %u failures\n", songs, checks, failures);
    return failures != 0;
}
