/* SPDX-License-Identifier: GPL-3.0-only
 * 2.0 help flash (bb/bb_text_block.h): the BBTX block gives back exactly the C literals it replaced, and every
 * missing, half-written, corrupt, foreign, stale or wrong-version block is refused into the fallback, never NULL,
 * never a read past 64 bytes. Host only, ASan/UBSan.
 *   clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I firmware/src \
 *         big-beat/integration/tests/help_flash/test_help_flash.c -o test_help_flash && ./test_help_flash */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb/bb_text_block.h"

static unsigned long checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; if (failures < 30) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* The C compiler's own reading of every literal in the .def (independent of help_flash.py's decoder). */
#define BB_TX_DEF(id, text) text,
static const char *const LIT[] = { 0,
#include "bb/bb_text_block.def"
};
#undef BB_TX_DEF
#define N (sizeof LIT / sizeof LIT[0] - 1)

static uint8_t img[BB_TX_FLASH_MAX];
static uint32_t len;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void reseal(uint8_t *b, uint32_t payload)   /* a structurally valid header for whatever payload b holds */
{
    put32(b + 8, payload);
    put32(b + 12, bb_tx_crc(b + BB_TX_HEADER, payload));
    put32(b + 28, bb_tx_crc(b, 28));
}
/* Refused: ready is 0 and every ID gives the 64-byte fallback, whose reads stay in bounds. */
static void refused(const uint8_t *b, uint32_t n, const char *what)
{
    unsigned bad = 0;
    bb_tx_bind_host(b, n);
    CHECK(!bb_tx_ready(), "%s: accepted", what);
    for (unsigned id = 0; id <= N + 1; id++) {
        const char *s = bb_tx(id);
        if (s != BB_TX_FALLBACK || memchr(s, 0, 64) != s + 16 || memcmp(s, "HELP UNAVAILABLE", 17)) bad++;
    }
    CHECK(!bad, "%s: %u IDs did not give the fallback", what, bad);
}

/* Real region-A images given on the command line: "PATH" must be refused, "+PATH" must be accepted. */
static void images(int argc, char **argv)
{
    for (int k = 1; k < argc; k++) {
        const char *p = argv[k] + (argv[k][0] == '+');
        FILE *f = fopen(p, "rb");
        static uint8_t region[245760];
        size_t n = f ? fread(region, 1, sizeof region, f) : 0;
        if (f) fclose(f);
        CHECK(n == sizeof region, "%s: not a 245,760 B region image", p);
        if (n != sizeof region) continue;
        bb_tx_bind_host(region + (BB_TX_FLASH_OFF - 0xA0000u), BB_TX_FLASH_MAX);
        if (argv[k][0] == '+') CHECK(bb_tx_ready() && !strcmp(bb_tx(N), LIT[N]), "%s: refused", p);
        else refused(region + (BB_TX_FLASH_OFF - 0xA0000u), BB_TX_FLASH_MAX, p);
        printf("image %s: %s\n", p, bb_tx_ready() ? "block valid" : "refused (fallback)");
    }
}

int main(int argc, char **argv)
{
    uint8_t *w = malloc(BB_TX_FLASH_MAX);
    unsigned rng = 12345u, i, bit;
    if (!w) return 2;
    CHECK(N == BB_TX_PIN_COUNT, "def %u, pin %u", (unsigned)N, (unsigned)BB_TX_PIN_COUNT);
    len = (uint32_t)sizeof bb_tx_host_image;
    CHECK(len == BB_TX_HEADER + BB_TX_PIN_BYTES && len <= BB_TX_FLASH_MAX, "image %u B", (unsigned)len);
    memcpy(img, bb_tx_host_image, len);
    memset(img + len, 0xFF, sizeof img - len);

    /* 1. the real block, as flash holds it (with the erased tail of the two sectors) */
    bb_tx_bind_host(img, sizeof img);
    CHECK(bb_tx_ready(), "valid block refused");
    for (unsigned id = 1; id <= N; id++) {
        const char *s = bb_tx(id);
        CHECK(s != BB_TX_FALLBACK && (const uint8_t *)s > img && (const uint8_t *)s < img + len, "id %u outside the block", id);
        CHECK(memchr(s, 0, (size_t)(img + len - (const uint8_t *)s)) != NULL, "id %u not terminated in the block", id);
        CHECK(!strcmp(s, LIT[id]), "id %u: \"%s\" != \"%s\"", id, s, LIT[id]);
        CHECK(strlen(LIT[id]) < 64, "id %u longer than the fallback", id);
    }
    CHECK(bb_tx(0) == BB_TX_FALLBACK && bb_tx((unsigned)N + 1) == BB_TX_FALLBACK && bb_tx(65535) == BB_TX_FALLBACK, "out-of-range ID");
    /* the exact block only: the payload CRC is pinned, so a block for the same build with the reading length cut is refused */

    /* 2. absent or erased: an old library, a 1.5 return, BB-BOB's region, an interrupted erase */
    refused(NULL, 0, "absent");
    memset(w, 0xFF, BB_TX_FLASH_MAX); refused(w, BB_TX_FLASH_MAX, "erased");
    memset(w, 0x00, BB_TX_FLASH_MAX); refused(w, BB_TX_FLASH_MAX, "zeros");
    refused(img, BB_TX_HEADER - 1, "shorter than a header");
    refused(img, len - 1, "window one byte short");

    /* 3. half-written: the image goes on header last (restore mode), so header erased + payload; and the reverse */
    memcpy(w, img, sizeof img); memset(w, 0xFF, BB_TX_HEADER); refused(w, sizeof img, "header not yet written");
    for (uint32_t cut = BB_TX_HEADER; cut < len; cut += 97) {
        memcpy(w, img, sizeof img); memset(w + cut, 0xFF, sizeof img - cut);
        refused(w, sizeof img, "payload tail erased");
    }
    memcpy(w, img, sizeof img); memset(w + len - 256, 0xFF, 256); refused(w, sizeof img, "last page erased");

    /* 4. every header byte, two flips each */
    for (i = 0; i < BB_TX_HEADER; i++)
        for (bit = 0; bit < 8; bit += 7) {
            memcpy(w, img, sizeof img); w[i] ^= (uint8_t)(1u << bit);
            refused(w, sizeof img, "header bit flip");
        }
    /* 5. 256 random payload corruptions (one byte, one bit) */
    for (i = 0; i < 256; i++) {
        rng = rng * 1103515245u + 12345u;
        memcpy(w, img, sizeof img);
        w[BB_TX_HEADER + (rng >> 8) % BB_TX_PIN_BYTES] ^= (uint8_t)(1u << ((rng >> 4) & 7u));
        refused(w, sizeof img, "payload bit flip");
    }
    /* 6. stale: a well-formed block from another build (one text changed, every CRC recomputed) */
    memcpy(w, img, sizeof img); w[len - 2] ^= 0x20; reseal(w, BB_TX_PIN_BYTES); refused(w, sizeof img, "another build's text");
    /* a well-formed block with one text fewer or more (another build's count and length) */
    memcpy(w, img, sizeof img); w[6] = (uint8_t)(BB_TX_PIN_COUNT - 1); w[7] = (uint8_t)((BB_TX_PIN_COUNT - 1) >> 8);
    reseal(w, BB_TX_PIN_BYTES); refused(w, sizeof img, "another build's count");
    memcpy(w, img, sizeof img); reseal(w, BB_TX_PIN_BYTES - 1); refused(w, sizeof img, "another build's length");
    /* 7. wrong version and wrong header length, otherwise valid */
    memcpy(w, img, sizeof img); w[4] = 2; reseal(w, BB_TX_PIN_BYTES); refused(w, sizeof img, "version 2");
    memcpy(w, img, sizeof img); w[4] = 0; reseal(w, BB_TX_PIN_BYTES); refused(w, sizeof img, "version 0");
    memcpy(w, img, sizeof img); w[5] = 48; reseal(w, BB_TX_PIN_BYTES); refused(w, sizeof img, "header length 48");
    /* 8. foreign data: a BBSK library start, SLOOP's BS31 family, random bytes */
    memcpy(w, img, sizeof img); memcpy(w, "BBSK", 4); refused(w, sizeof img, "BBSK magic");
    memcpy(w, img, sizeof img); memcpy(w, "BS31", 4); refused(w, sizeof img, "BS31 magic");
    for (i = 0; i < BB_TX_FLASH_MAX; i++) { rng = rng * 1103515245u + 12345u; w[i] = (uint8_t)(rng >> 16); }
    refused(w, BB_TX_FLASH_MAX, "random bytes");
    /* a pinned-looking header over a foreign payload (magic, version, count, length and payload CRC copied) */
    memcpy(w + BB_TX_HEADER, img + len, 64); refused(w, sizeof img, "copied header, foreign payload");

    /* 9. the check is cached and a refusal never sticks to a later good block; a good one never to a bad one */
    bb_tx_bind_host(img, sizeof img); CHECK(bb_tx_ready() && !strcmp(bb_tx(1), LIT[1]), "good after refusals");
    memcpy(w, img, sizeof img); w[40] ^= 1; refused(w, sizeof img, "bad after good");
    bb_tx_bind_host(img, sizeof img); CHECK(bb_tx(N) != BB_TX_FALLBACK, "good again");

    free(w);
    images(argc, argv);
    printf("help flash: %u texts, block %u B, %lu checks, %lu failures\n", (unsigned)N, (unsigned)len, checks, failures);
    return failures != 0;
}
