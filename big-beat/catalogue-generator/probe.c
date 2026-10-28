/* SPDX-License-Identifier: GPL-3.0-only
 * r9 catalogue probe (host only, never touches hardware). Runs the firmware's own catalogue read paths,
 * from rc2's firmware/src/bb, over every section of a BBPC file and prints one TSV row per part:
 *   bb_catalogue_open + bb_browser_init      (startup checks; once)
 *   bb_factory_read_drum / bb_factory_project_drum   (trio/808 grid path: result, issues, length, grid digest)
 *   bb_factory_prepare_rich_drum             (rich phrase path used by playback: result, index digest)
 *   bb_factory_prepare_synth_data synth 0/1  (Dillo "bass" / Telegram "lead": result, length, notes+expression digest)
 *   bb_factory_read_recipe (eras)            (era preset: result, recipe digest)
 * usage: probe CATALOGUE.bbpc */
#include <stdio.h>
#include <string.h>
#include "bb_browser.h"
#include "bb_factory_drum.h"
#include "bb_factory_drum_launch.h"
#include "bb_factory_pattern.h"
#include "bb_factory_recipe.h"
#include "bb_phrase_index.h"
#include "bb_phrase.h"
#include "bb_audio_phrase.h"
#include "bb909_sequence.h"
#include "bb_factory_trio.h"
static bb_pc_cache cache; static bb_catalogue cat;
static bb_factory_drum_data drum; static bb_factory_drum_grid grid; static bb_phrase_index ix;
static bb_pattern_data synth; static bb_phrase ph; static bb_phrase_index tmp; static bb_factory_recipe recipe; static bb_factory_drum_grid grid909;
static bb_core inherited; static bb_trio_program trio; static bb_factory_trio_workspace twork; static bb_factory_trio_report treport;
/* The launch half of bb_factory_drum_launch.c submits to a live bb_audio; the probe never calls it. */
#include "bb_audio.h"
uint32_t bb_audio_phrase_source_voices(struct bb_audio *a,unsigned m){(void)a;(void)m;return 0;}
int bb_audio_phrase_preflight(const bb_phrase_index *p){(void)p;return 0;}
int bb_audio_phrase_submit(bb_audio *a,unsigned m,const bb_phrase_index *p,unsigned b,uint32_t *t){(void)a;(void)m;(void)p;(void)b;(void)t;return 0;}
int bb_audio_drum_pattern_submit(bb_audio *a,unsigned m,const bb_drum_pattern_data *g,unsigned p,uint32_t *t){(void)a;(void)m;(void)g;(void)p;(void)t;return 0;}
static unsigned long long fnv(const void *p, size_t n) {
    const unsigned char *b = p; unsigned long long h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}
int main(int argc, char **argv) {
    static uint8_t buf[1 << 20];
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL; if (!f) return 2;
    size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (!bb_catalogue_open(&cat, buf, n, &cache)) { puts("FAIL open"); return 1; }
    bb_browser b; if (!bb_browser_init(&b, &cat)) { puts("FAIL browser_init"); return 1; }
    const char *groups[2] = {"eras", "songs"};
    printf("group\tbank\tpart\tname\tread\tissues\tgrid_len\tgrid\tissues909\trich\tvoices\t808_native\t808\trepr\trich_ix\ts0\ts0_len\ts0_data\ts1\ts1_len\ts1_data\trecipe\trecipe_data\n");
    for (unsigned g = 0; g < 2; g++) {
        bb_catalogue_id arr = bb_catalogue_find(&cat, (bb_catalogue_id)cat.container.root, groups[g]);
        for (size_t bk = 0; bk < bb_catalogue_count(&cat, arr); bk++) {
            bb_catalogue_id bank = bb_catalogue_at(&cat, arr, bk), parts = bb_catalogue_find(&cat, bank, "parts");
            for (size_t p = 0; p < bb_catalogue_count(&cat, parts); p++) {
                bb_factory_choice ch = {&cat, bank, bb_catalogue_at(&cat, parts, p), (uint16_t)bk, (uint16_t)p, (uint8_t)g};
                if (!bb_factory_choice_valid(&cat, &ch)) { printf("FAIL choice %u %zu %zu\n", g, bk, p); return 1; }
                char name[96] = "?"; bb_catalogue_copy_text(&cat, bb_catalogue_find(&cat, ch.part, "name"), name, sizeof name);
                memset(&grid, 0, sizeof grid); memset(&ix, 0, sizeof ix);
                int rd = bb_factory_read_drum(&cat, &ch, &drum); unsigned issues = 99;
                if (!rd) issues = bb_factory_project_drum(&drum, &grid);
                unsigned i909 = 99; if (!rd) { memset(&grid909, 0, sizeof grid909); i909 = bb909_sequence_project(&drum, &grid909); }
                int rich = bb_factory_prepare_rich_drum(&cat, &ch, &ix);
                unsigned long long ixd = rich ? 0ull : fnv(&ix, sizeof ix); uint32_t voices = rich ? 0u : ix.voices;
                int vn = -1, v8 = -1, vr = -1;
                if (!rd && bb_phrase_pack_shaker(&drum, &ph)) {
                    vn = bb_phrase_index_prepare(&ph, BB_RICH_808_NATIVE_VOICES, &tmp);
                    v8 = bb_phrase_index_prepare(&ph, BB_RICH_808_VOICES, &tmp);
                    vr = bb_phrase_index_prepare(&ph, BB_RICH_REPRESENTABLE_VOICES, &tmp);
                }
                printf("%s\t%zu\t%zu\t%s\t%d\t%u\t%u\t%016llx\t%u\t%d\t%08x\t%d\t%d\t%d\t%016llx", groups[g], bk, p, name, rd, issues,
                       issues ? 0u : grid.length, issues ? 0ull : fnv(&grid, sizeof grid), i909, rich, (unsigned)voices, vn, v8, vr, ixd);
                for (unsigned s = 0; s < 2; s++) {
                    memset(&synth, 0, sizeof synth);
                    int r = bb_factory_prepare_synth_data(&cat, &ch, s, &synth);
                    printf("\t%d\t%u\t%016llx", r, r ? 0u : synth.length, r ? 0ull : fnv(&synth, sizeof synth));
                }
                if (g == 0) { memset(&recipe, 0, sizeof recipe); int r = bb_factory_read_recipe(&cat, &ch, &recipe);
                    printf("\t%d\t%016llx\n", r, r ? 0ull : fnv(&recipe, sizeof recipe)); }
                else printf("\t-\t-\n");
            }
        }
    }
    /* HOME era load (bb_home_input.inc -> bb_performer -> bb_factory_request_trio): the whole era, all 8 parts. */
    if (!bb_init(&inherited, 48000)) { puts("FAIL bb_init"); return 1; }
    bb_catalogue_id eras = bb_catalogue_find(&cat, (bb_catalogue_id)cat.container.root, "eras");
    for (size_t bk = 0; bk < bb_catalogue_count(&cat, eras); bk++) {
        bb_catalogue_id bank = bb_catalogue_at(&cat, eras, bk), parts = bb_catalogue_find(&cat, bank, "parts");
        bb_factory_choice ch = {&cat, bank, bb_catalogue_at(&cat, parts, 0), (uint16_t)bk, 0, 0};
        char id[64] = "?"; bb_catalogue_copy_text(&cat, bb_catalogue_find(&cat, bank, "id"), id, sizeof id);
        int r0 = bb_factory_prepare_trio(&cat, &ch, &inherited, 0, 0, &trio, &twork, &treport);
        int r3 = bb_factory_prepare_trio(&cat, &ch, &inherited, 0, BB_FTRIO_THREE_ENGINE, &trio, &twork, &treport);
        printf("trio\t%zu\t%s\t%d\t%d\t%016llx", bk, id, r0, r3, r3 ? 0ull : fnv(&trio, sizeof trio));
        for (unsigned k = 0; k < 8; k++) printf("\t%u/%u/%u/%u", treport.part[k].drum_result, treport.part[k].drum_issues,
                                                treport.part[k].synth_result[0], treport.part[k].synth_result[1]);
        printf("\n");
    }
    return 0;
}
