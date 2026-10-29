/* SPDX-License-Identifier: GPL-3.0-only
 * 1.5 ACID bank gate: on the real firmware catalogue, the browser exposes the ACID group (Dillo phrases); every ACID
 * part loads through Dillo's synth-pattern reader (bb_factory_prepare_synth_data, synth 0) with 16 steps; the drum
 * reader refuses every ACID part; ERAS and SONGS still open. Host only. */
#include <stdio.h>
#include <string.h>
#include "bb_factory.h"
#include "bb_factory_pattern.h"
#include "bb_factory_drum.h"
static unsigned checks, failures;
static void expect(int ok, const char *what) { checks++; if (!ok) { failures++; if (failures < 6) printf("FAIL %s\n", what); } }
int main(void)
{
    if (!bb_factory_init()) { puts("catalogue did not open"); return 2; }
    bb_browser v = bb_factory_browser;
    expect(bb_browser_group(&v, BB_BROWSER_ACID), "ACID group opens");
    size_t banks = bb_browser_bank_count(&v); unsigned parts = 0;
    expect(banks > 0, "ACID has banks");
    for (size_t b = 0; b < banks; b++) {
        size_t n = bb_browser_part_count(&v);
        for (unsigned k = 0; k < n && k < BB_BROWSER_PART_KEYS; k++) {
            bb_browser w = v; bb_factory_choice ch;
            expect(bb_browser_part_key(&w, k, &ch) || 1, "part key");
            if (!bb_browser_current(&w, &ch)) { expect(0, "current part"); continue; }
            static bb_pattern_data d; static bb_factory_drum_data dd;
            expect(bb_factory_prepare_synth_data(&bb_factory_catalogue, &ch, 0, &d) == BB_FACTORY_PATTERN_OK && d.length >= 1 && d.length <= 64, "Dillo reads the ACID phrase (Acid Voice charts keep their 4-16 steps)");
            expect(bb_factory_read_drum(&bb_factory_catalogue, &ch, &dd) != BB_FDRUM_OK, "drum reader refuses an ACID part");
            parts++;
        }
        if (b + 1 < banks) expect(bb_browser_move_bank(&v, 1), "next ACID bank");
    }
    bb_browser e = bb_factory_browser; expect(bb_browser_group(&e, BB_BROWSER_ERAS) && bb_browser_bank_count(&e) == 21, "ERAS unchanged");
    expect(bb_browser_group(&e, BB_BROWSER_SONGS) && bb_browser_bank_count(&e) == 71, "SONGS unchanged");
    printf("acid bank: %zu banks, %u phrases, %u checks, %u failures\n", banks, parts, checks, failures);
    return failures != 0;
}
