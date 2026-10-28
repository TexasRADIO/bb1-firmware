/* SPDX-License-Identifier: GPL-3.0-only
 * Big Beat derived-target recovery adapter. FELUCCA source is GPL-3.0-only.
 * This is a fresh minimal USB updater path informed by the AMB-1 safe-boot
 * design; it does not copy AMB implementation code. */
#include "bb_recovery_policy.h"

static uint8_t bb_recovery_safe_mode;

/* Included in main.c after its hardware/init helpers and before fm1_main().
 * In safe mode we intentionally do not call persist_boot, settings_init,
 * felucca_init, master_boot, any Big Beat init, or audio_init. */
static void bb_recovery_usb_safe_main(void)
{
    uint32_t t0, f, release_since = 0;
    uint8_t play_armed = 0;
    /* TIMER5's normal ISR uses this retained audio flag to decide whether USB
     * polling must defer. Safe mode has no audio ISR, so clear it explicitly. */
    felucca_dbg.in_audio = 0;
    fm1_audio_stop();
    f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    irq_restore(f);

    lcd_init();
    palette_set(UI_GREY_INDEX);
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 58, 240, &AF_M, "USB SAFE MODE", T_THEME, 1);
    draw_text_box(0, 92, 240, &AF_S, "M-UPGRADE READY", T_TEXT, 1);
    draw_text_box(0, 142, 240, &AF_S, "PLAY: RETRY NORMAL", T_TEXT, 1);
    lcd_sync();
    fm1_input_init();
    panel_init();
    usb_start();
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    t0 = fm1_ms;

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        /* A healthy safe-mode loop is not another failed application start. */
        if (bootguard.pending && (uint32_t)(fm1_ms - t0) >= 10000u)
            bootguard.pending = 0;

        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();
        }

#if FELUCCA_CDC
        /* Recovery diagnostics must not enter the ordinary console, whose
         * commands expect initialized song, voices and instrument state. */
        if (cdc_recovery_task()) {
            bootguard.failed = 0;
            bootguard.pending = 0;
            fm1_reboot();
        }
#endif

        /* PLAY is the explicit request to retry the normal application. */
        if (!(fm1_in.buttons & (1u << panel.btn[B_PLAY]))) {
            if (!release_since)
                release_since = fm1_ms ? fm1_ms : 1u;
            else if ((uint32_t)(fm1_ms - release_since) >= 500u)
                play_armed = 1;
        } else {
            release_since = 0;
        }
        if (play_armed && (fm1_in.buttons & (1u << panel.btn[B_PLAY]))) {
            bootguard.failed = 0;
            bootguard.pending = 0;
            fm1_reboot();
        }
        /* Intentionally ignore usb.uboot_req here; the chip UBOOT route is
         * not treated as the recoverable software updater. */
    }
}
