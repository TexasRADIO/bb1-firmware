/* SPDX-License-Identifier: GPL-3.0-only
 * Level and noise of every voice in a BBSK sample pack, decoded exactly as the firmware plays it:
 * bb_sample_pack_valid / bb_sample_pack_bank (pack parse), bb_sample_kit_trigger / _tick (IMA ADPCM,
 * half-rate interpolation) at full velocity, 44.1 kHz. Reports peak and RMS (dBFS of int16 full
 * scale) of the decoded voice, the tail floor (RMS of its last 20 ms), and the same peak after the
 * installed resident gain 0.25 and the dry pan 0.24 (the louder side), i.e. what reaches the mix
 * before machine level, bus and master.
 * usage: measure_kits PACK.bbsk [installed region length]  (CSV on stdout) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb_sample_pack_r20.h"

static double db(double x) { return x > 0 ? 20.0 * log10(x) : -200.0; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    size_t total = bb_fsmp_u32(buf + 8);
    if (total <= n) n = total;                       /* the pack inside a padded flash region */
    if (!bb_sample_pack_valid(buf, n)) { fprintf(stderr, "pack invalid\n"); return 1; }
    printf("kit,kit_name,voice,note,source_frames,source_rate_q16,peak_dbfs,rms_dbfs,tail_rms_dbfs,peak_after_0.25x0.24_dbfs\n");
    for (unsigned k = 0; k < buf[6]; k++) {
        const uint8_t *e = buf + 32 + k * 28;
        char name[13]; memcpy(name, e + 12, 12); name[12] = 0;
        bb_sample_zone z[16]; unsigned nz;
        if (!bb_sample_pack_bank(buf, n, k, z, &nz)) { fprintf(stderr, "bank %u invalid\n", k); return 1; }
        for (unsigned v = 0; v < nz; v++) {
            bb_sample_slot slot[1]; memset(slot, 0, sizeof slot);
            if (bb_sample_kit_trigger(slot, 1, z, nz, z[v].note, 127) != BB_SK_OK) continue;
            static int32_t out[400000]; unsigned len = 0;
            while (slot[0].voice.active && len < 400000u) { int32_t s; bb_sample_kit_tick(slot, 1, &s); out[len++] = s; }
            double peak = 0, sum = 0, tail = 0; unsigned tn = len < 882u ? len : 882u;
            for (unsigned i = 0; i < len; i++) { double a = fabs(out[i] / 32768.0); if (a > peak) peak = a; sum += a * a; }
            for (unsigned i = len - tn; i < len; i++) { double a = out[i] / 32768.0; tail += a * a; }
            /* RMS over the sounding part: samples within 40 dB of the peak envelope start */
            double rms = sqrt(sum / (len ? len : 1));
            printf("%u,%s,%u,%u,%u,%u,%.1f,%.1f,%.1f,%.1f\n", k, name, v, z[v].note, z[v].frames, z[v].rate,
                   db(peak), db(rms), db(sqrt(tail / (tn ? tn : 1))), db(peak * 0.25 * 0.24));
        }
    }
    return 0;
}
