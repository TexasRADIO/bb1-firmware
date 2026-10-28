/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware side of ota.c (FELUCCA_OTA): clock and watchdog, erases and writes only inside the
 * OTA area, the progress screen, the commit (record into RAM, core reset). The host test
 * (ota_test.c) provides its own. */
static uint32_t ota_now_ms(void) { return fm1_ms; }
/* Definition follows this adapter in felucca.c. paused=1 is intentional:
 * neither recovery startup nor an update may touch bb_sound's command queue.
 * The normal consumer releases sources and discards held arrivals on return. */
static void bb_midi_input(int paused, int suppressed);
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (ota_midi_owned)
        bb_midi_input(1, 1);
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    char b[24];
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 92, 240, &AF_M, "UPDATE", T_TEXT, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &AF_S, STEP[step < 6u ? step : 0], T_THEME, 1);
        return;
    }
    if (code == 1)
        str_cpy(b, "DRY RUN OK", sizeof b);
    else {
        uint32_t k = 0, v = (uint32_t)-code;
        str_cpy(b, "FAILED  -", sizeof b);
        while (b[k])
            k++;
        if (v >= 10u)
            b[k++] = (char)('0' + v / 10u);
        b[k++] = (char)('0' + v % 10u);
        b[k] = 0;
    }
    draw_text_box(0, 124, 240, &AF_S, b, T_THEME, 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    /* Called only after the complete package and durable update record have
     * verified. A replacement app gets its own startup attempt; failures of
     * the previous app must not strand a successful update in safe mode.
     * Keep the crash record intact. Refused/incomplete updates never reach here. */
    bootguard.failed = 0;
    bootguard.pending = 0;                              /* intentional reset */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                             /* record into RAM, core reset (fm1_sys.h) */
}
