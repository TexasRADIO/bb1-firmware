/* SPDX-License-Identifier: GPL-3.0-only
 * BB1's full-install loader on the host. A simulated host serves the real
 * .fwsc while a simulated one-MiB NOR enforces the loader's write bounds.
 * No hardware is contacted. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000], *logical;
static uint8_t before[0x100000], rx[1024];
static size_t logical_len;
static uint32_t rx_len, rx_full, now_ms, erases, progs, bad_range, record_cleared, f0_asked;
static int cut_after = -1;
static jmp_buf cut;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, bits = 0, used = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << bits;
        bits += 8u;
        while (bits >= 7u) { out[used++] = (uint8_t)(acc & 0x7Fu); acc >>= 7; bits -= 7u; }
    }
    if (bits) out[used++] = (uint8_t)(acc & 0x7Fu);
    return used;
}

static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, bits = 0, used = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << bits;
        bits += 7u;
        if (bits >= 8u) { out[used++] = (uint8_t)acc; acc >>= 8; bits -= 8u; }
    }
    return used;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t decoded[64], message[600];
    uint32_t address, length, i, sum = 0;
    unpack7(p + 1, n - 2, decoded);
    if (decoded[2] == 0x11) return 0;
    address = decoded[7] | decoded[8] << 8 | decoded[9] << 16 | (uint32_t)decoded[10] << 24;
    length = decoded[11] | decoded[12] << 8 | decoded[13] << 16;
    memcpy(message, "\x00\x59\x30", 3);
    message[3] = (uint8_t)(length + 8u); message[4] = (uint8_t)((length + 8u) >> 8);
    message[5] = message[6] = 0;
    memcpy(message + 7, &address, 4);
    message[11] = (uint8_t)length; message[12] = (uint8_t)(length >> 8); message[13] = 0;
    if (address >= 0xE0000000u) {
        f0_asked += address == 0xF0000000u;
        memset(message + 14, 0, length);
        memcpy(message + 14, "success", 8);
    } else {
        if (address + length > logical_len) exit(2);
        memcpy(message + 14, logical + address, length);
    }
    for (i = 6; i < 14 + length; i++) sum += message[i];
    message[14 + length] = (uint8_t)~sum;
    rx_len = pack7(message, 15 + length, rx);
    rx_full = 1;
    return 0;
}

static int ota_frame_get(const uint8_t **p, uint32_t *n)
{ if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{ (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n)
{ memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#if defined(__APPLE__) && !defined(section)
#define section(name) used
#define TEST_HUB_DEFINED_SECTION 1
#endif
#include "firmware/src/ota.c"
#if defined(TEST_HUB_DEFINED_SECTION)
#undef section
#undef TEST_HUB_DEFINED_SECTION
#endif

static int in_write_range(uint32_t off, uint32_t n)
{
    return (off >= 0x4000u && off + n <= 0x93000u) ||
           (off >= 0xA0000u && off + n <= 0xDC000u);
}

static uint32_t ldr_flash_id(void) { return 0x856014u; }
static int ldr_fread(uint32_t off, void *p, uint32_t n)
{ memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if ((off & 0xFFFu) || (!in_write_range(off, 0x1000u) && off != 0xE4000u)) bad_range++;
    erases++;
    memset(nor + off, 0xFF, 0x1000u);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *src = p;
    uint32_t i;
    if (!in_write_range(off, n) || (off & 0xFFu) + n > 256u) bad_range++;
    if (cut_after >= 0 && off >= 0xA0000u && off < 0xDC000u && cut_after-- == 0) {
        for (i = 0; i < n / 2u; i++) nor[off + i] &= src[i];
        longjmp(cut, 1);
    }
    progs++;
    for (i = 0; i < n; i++) nor[off + i] &= src[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total)
{ if (done > total) exit(2); }
#include "firmware/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000u), *result;
    size_t bytes, i;
    if (!file) { perror(path); exit(2); }
    bytes = fread(raw, 1, 0x200000u, file);
    fclose(file);
    result = malloc(bytes);
    for (i = 0; i < 20; i++) memcpy(result + i * 47, raw + i * 48, 47);
    memcpy(result + 20 * 47, raw + 20 * 48, bytes - 20 * 48);
    *length = bytes - 20;
    free(raw);
    return result;
}

static uint32_t flash_offset(const uint8_t *image, uint32_t *size)
{
    uint8_t header[0x400];
    uint32_t i, count;
    memcpy(header, image, sizeof header);
    ota_jl_enc(header, 0x40);
    count = ota_rd16(header + 8);
    for (i = 0; i < count; i++) {
        uint8_t *entry = header + 0x40 + i * 0x50u;
        ota_jl_enc(entry, 0x50);
        if (ota_rd16(entry) == 0) { *size = ota_rd32(entry + 12); return ota_rd32(entry + 8); }
    }
    return 0;
}

static uint32_t seed;
static void pattern(uint32_t lo, uint32_t hi)
{
    for (; lo < hi; lo++) { seed = seed * 1103515245u + 12345u; nor[lo] = (uint8_t)(seed >> 16); }
}
static void put_record(void)
{
    uint8_t record[112] = {0};
    record[2] = 0x0D; record[3] = 0x5A; record[4] = 0x01; record[5] = 0x5A;
    record[6] = 0x41; record[7] = 0x54;
    ota_wr16(record, ota_crc16(record + 2, 78, 0));
    memcpy(nor + 0xE4F00u, record, sizeof record);
}

static uint32_t package_flash, package_flash_size;
static void device(void)
{
    seed = 0x42423131u;
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, logical + package_flash, 0x4000u);
    pattern(0x4000u, sizeof nor);
    put_record();
    memcpy(before, nor, sizeof nor);
}

static int installed(void)
{
    return !memcmp(nor + 0x4000u, logical + package_flash + 0x4000u, 0x93000u - 0x4000u) &&
           !memcmp(nor + 0xA0000u, logical + package_flash + 0xA0000u, 0x3C000u);
}
static int kept(void)
{
    uint32_t sector;
    for (sector = 0; sector < sizeof nor; sector += 0x1000u)
        if (!in_write_range(sector, 0x1000u) && sector != 0xE4000u &&
            memcmp(nor + sector, before + sector, 0x1000u)) return 0;
    return 1;
}
static int check(int ok, const char *label)
{ printf("  %-62s %s\n", label, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

int main(int argc, char **argv)
{
    int failures = 0, result;
    uint32_t cut_page, sound_pages = (0xDC000u - 0xA0000u) / 256u;
    if (argc != 2) { fprintf(stderr, "usage: %s PACKAGE.fwsc\n", argv[0]); return 2; }
    logical = load_logical(argv[1], &logical_len);
    package_flash = flash_offset(logical, &package_flash_size);
    failures += check(package_flash_size == 0x100000u, "package carries a one-MiB flash image");

    device();
    result = ldr_session();
    failures += check(result == 0, "complete install succeeds");
    failures += check(installed(), "application and exact BB1 library are installed");
    failures += check(record_cleared && nor[0xE4F06u] == 0xFF, "update record is cleared after success");
    failures += check(kept(), "every byte outside application and library ranges is preserved");
    failures += check(!bad_range, "no erase or write leaves the two declared ranges");
    failures += check(f0_asked, "loader completes the host handshake");

    memcpy(before, nor, sizeof nor);
    erases = progs = 0;
    result = ldr_session();
    failures += check(result == 0 && erases == 0 && progs == 0, "second install skips identical sectors");
    failures += check(!memcmp(nor, before, sizeof nor), "second install changes no flash bytes");

    for (cut_page = 0; cut_page < 3; cut_page++) {
        const int at = cut_page == 0 ? 0 : cut_page == 1 ? (int)(sound_pages / 2u) : (int)(sound_pages - 1u);
        device();
        cut_after = at;
        if (!setjmp(cut)) { ldr_session(); failures += check(0, "power cut reached"); continue; }
        cut_after = -1;
        result = ldr_session();
        failures += check(result == 0 && installed(), "power-cut rerun completes the install");
        failures += check(kept() && !bad_range, "power-cut rerun preserves all other flash");
    }

    device();
    logical[package_flash + 0xA0010u] ^= 0x5Au;
    erases = progs = 0;
    result = ldr_session();
    failures += check(result == -12 && erases == 0 && progs == 0,
                      "damaged library is refused before any erase");

    printf("BB1 Hub loader: %s\n", failures ? "FAILED" : "all checks passed");
    return failures != 0;
}
