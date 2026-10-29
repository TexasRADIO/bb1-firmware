/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Serial console on the CDC-ACM function (FELUCCA_CDC=1): read-only
 * diagnostics. Runs in the main loop (cdc_task); usb_poll moves the bytes.
 * The baud rate is ignored. Nothing here writes memory or flash; `uboot`
 * does what the SysEx soft key does. */
#define CON_LINE 96u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

/* BB1 one-click install: while the MIDI editor tunnel runs a console line, output goes to its buffer. */
static char *con_cap;
static uint32_t con_cap_n, con_cap_max;
static uint8_t bb_asset_reboot_pending;

static void con_putc(char c)
{
    if (con_cap) {
        if (con_cap_n < con_cap_max)
            con_cap[con_cap_n++] = c;
        return;
    }
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok || a < 0x93000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x93000..0xFFFFF)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

#if FELUCCA_UAC
static void con_uac(void)                              /* USB audio input: stream state and glitches */
{
    uint32_t p = uac.pkts, base = uac.r48 ? 48u : 44u;
    con_kv("uac_alt", uac.alt);
    con_kv("uac_hz", uac.r48 ? 48000 : 44100);      /* the rate the host set (SET_CUR; 44100 until it does) */
    con_kv("uac_rate_sets", (int32_t)uac.rate_sets);
    con_kv("uac_starts", (int32_t)uac.starts);
    con_kv("uac_pkts", (int32_t)p);
    con_kv("uac_rate_hz", p ? (int32_t)base * 1000 + (int32_t)(uac.frames - base * p) * 1000 / (int32_t)p : 0);   /* < 5 h */
    con_kv("uac_underruns", (int32_t)uac.underruns);
    con_kv("uac_overruns", (int32_t)uac.overruns);
    con_kv("uac_missed", (int32_t)uac.missed);
    con_kv("uac_stalls", (int32_t)uac.stalls);
    con_kv("uac_adj_up", (int32_t)uac.adj_up);
    con_kv("uac_adj_down", (int32_t)uac.adj_down);
    con_kv("uac_fill", (int32_t)uac.fill_min);
    con_kv("uac_fill_lo", uac.fill_lo == 0xFFFFFFFFu ? -1 : (int32_t)uac.fill_lo);
    con_kv("uac_fill_hi", (int32_t)uac.fill_hi);
}
#endif

static void con_status(void)
{
    /* Big Beat release: the stock engine table is not linked. Every stock
     * track keeps eng_req == 0 and preset == 0 (zeroed .bss; no live writer),
     * so the original lookup always printed ENGINES[0]->name and
     * ENG_ANALOG.presets[0].name. The same bytes are printed here.
     * claude-code/reduction/DELETION-MAP.md item D2. */
    con_puts("felucca ");
    con_puts(FELUCCA_VERSION);
    con_puts("\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("audio_max_us", (int32_t)felucca_dbg.max_us);
    con_kv("audio_late", (int32_t)felucca_dbg.late);
    con_kv("voices_shed", (int32_t)shed_count);
    con_kv("voices_given_up", (int32_t)voice_kills);
    con_kv("track", (int32_t)song.sel + 1);
    con_kv("batt_raw", song.batt_raw);
    con_puts("engine ");
    con_puts("ANALOG");
    con_puts("\r\n");
    con_puts("preset ");
    con_puts("SAW LEAD");
    con_puts("\r\n");
    con_kv("bpm", song.g[G_BPM]);
    con_kv("playing", song.playing);
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("usb_sof", (int32_t)usb.sof_seen);
    con_kv("usb_suspends", (int32_t)usb.suspends);
    con_kv("usb_retries", (int32_t)usb.retries);
    con_kv("usb_frame", (int32_t)usb.frame);
    con_kv("usb_frame_stalls", (int32_t)usb.frame_stalls);
    con_kv("usb_max_gap_polls", (int32_t)usb.max_gap);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("midi_tx_pkts", (int32_t)usb.tx_pkts);
#if FELUCCA_UAC
    con_uac();
#endif
    con_kv("uart_enabled", FELUCCA_UART);
#if FELUCCA_UART
    con_kv("uart_rx_bytes", (int32_t)um.bytes);
    con_kv("uart_rx_msgs", (int32_t)um.msgs);
    con_kv("uart_rx_drops", (int32_t)um.drops);
#endif
#if FELUCCA_FLASH
    con_kv("flash", flash_ok);
#endif
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&felucca_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late",
                                        "timer_irqs", "ui_frames", "last_us", "cpu_q8", "boots",
                                        "stage", "page", "home", "prev_stage", "prev_page",
                                        "prev_home", "prev_rst", "prev_frames"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof felucca_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
#if FELUCCA_UAC
    con_uac();
#endif
}

/* input scan (hal/fm1_input.h fm1_in_stat) since the last `inp`, then cleared: tick gaps (= LED
 * on-time of a column), per-column LED share in 1/1000, the tick cost, the dim pulse (its average width and the
 * bits of the 595 shift it spans), encoder transitions */
static void con_inp(void)
{
    static uint32_t t_last;
    uint32_t now = fm1_ticks(), win = now - t_last, i, k;
    uint32_t win_us = win / FM1_TICKS_PER_US;
    t_last = now;
    con_kv("window_ms", (int32_t)(win_us / 1000u));
    con_kv("ticks_per_s", (int32_t)(win_us >= 1000u ? fm1_in_stat.ticks * 1000u / (win_us / 1000u) : 0u));
    con_kv("gap_max_us", (int32_t)(fm1_in_stat.gap_max / FM1_TICKS_PER_US));
    con_puts("gap_hist <.15 .3 .6 1.2 2.4 4.8 9.6 more ms:");
    for (i = 0; i < FM1_GAP_BINS; i++) {
        con_putc(' ');
        con_dec((int32_t)fm1_in_stat.gap_hist[i]);
    }
    con_puts("\r\n");
    con_kv("tick_cost_avg_ns", (int32_t)(fm1_in_stat.ticks ? fm1_in_stat.cost_sum / fm1_in_stat.ticks * 1000u / FM1_TICKS_PER_US : 0u));
    con_kv("tick_cost_max_us", (int32_t)(fm1_in_stat.cost_max / FM1_TICKS_PER_US));
    con_kv("tick_cpu_permille", (int32_t)(win >= 1000u ? fm1_in_stat.cost_sum / (win / 1000u) : 0u));
    con_kv("dim_pulse_ns", (int32_t)(fm1_in_stat.dim_n ? fm1_in_stat.dim_sum / fm1_in_stat.dim_n * 1000u / FM1_TICKS_PER_US : 0u));
    con_kv("dim_bits", (int32_t)fm1__dim_k);
    con_puts("led_on_permille:");
    for (i = 0; i < FM1_NCOL; i++) {
        con_putc(' ');
        con_dec((int32_t)(win >= 1000u ? fm1_in_stat.on[i] / (win / 1000u) : 0u));
    }
    con_puts("\r\nled_on_max_us:");
    for (i = 0; i < FM1_NCOL; i++) {
        con_putc(' ');
        con_dec((int32_t)(fm1_in_stat.on_max[i] / FM1_TICKS_PER_US));
    }
    con_puts("\r\nenc moves/lost/detent state:");
    for (k = 0; k < FM1_NENC; k++) {
        con_putc(' ');
        con_dec((int32_t)fm1_in_stat.enc_moves[k]);
        con_putc('/');
        con_dec((int32_t)fm1_in_stat.enc_lost[k]);
        con_putc('/');
        con_dec(fm1_in.enc_rest[k]);
    }
    con_puts("\r\n");
    {   /* note keys, from the first scan that saw the contact (us): debounced press, note-on, DAC out */
        uint32_t n = fm1_in_stat.press_n, m = fm1_in_stat.kb_n;
        con_kv("key_presses", (int32_t)n);
        con_kv("key_press_avg_us", (int32_t)(n ? fm1_in_stat.press_sum / n / FM1_TICKS_PER_US : 0u));
        con_kv("key_press_max_us", (int32_t)(fm1_in_stat.press_max / FM1_TICKS_PER_US));
        con_kv("key_notes", (int32_t)m);
        con_kv("key_noteon_avg_us", (int32_t)(m ? fm1_in_stat.kb_sum / m / FM1_TICKS_PER_US : 0u));
        con_kv("key_noteon_max_us", (int32_t)(fm1_in_stat.kb_max / FM1_TICKS_PER_US));
        con_kv("key_dac_avg_us", (int32_t)(m ? fm1_in_stat.dac_sum / m / FM1_TICKS_PER_US : 0u));
        con_kv("key_dac_max_us", (int32_t)(fm1_in_stat.dac_max / FM1_TICKS_PER_US));
    }
    con_kv("timer_irqs", (int32_t)felucca_dbg.timer_irqs);
    con_kv("nested", (int32_t)felucca_dbg.nested);
    {
        uint32_t k2 = fm1__lock();
        memset((void *)&fm1_in_stat.gap_max, 0, sizeof fm1_in_stat - sizeof fm1_in_stat.last);
        fm1__unlock(k2);
    }
}

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("dbg", fm1_crash.dbg);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kx("icfg", fm1_crash.icfg);
    con_kx("etm0", fm1_crash.etm[0]);
    con_kx("etm1", fm1_crash.etm[1]);
    con_kx("etm2", fm1_crash.etm[2]);
    con_kx("etm3", fm1_crash.etm[3]);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
    if (fm1_crash_extra_matches(&fm1_crash_extra,fm1_crash.count)) {
        con_kv("crashx_version",1);
        for(unsigned i=0;i<16u;i++) {
            con_puts("crashx_r");con_dec((int32_t)i);con_putc(' ');
            con_hex(fm1_crash_extra.r[i],8);con_puts("\r\n");
        }
        con_kx("crashx_usp",fm1_crash_extra.usp);
        con_kx("crashx_ssp",fm1_crash_extra.ssp);
    }
}

#if FELUCCA_TRACKS
static void con_params(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}
#endif

#include "bb_target_diagnostics.c"
#include "bb_target_asset_console.c"

/* The web installer's sound step over USB-MIDI (editor command 80), so one click installs everything.
 * Only the sound-library writer and a bounded read of its own region are reachable; every check of the
 * serial path (safe state, audio hold, chunk CRCs, commit verify) is the same code. *reboot asks the
 * caller to restart after its reply has gone out. */
static int con_prefix(const char *s, const char *w) { while (*w) if (*s++ != *w++) return 0; return 1; }
static uint32_t con_tunnel(const char *line, char *out, uint32_t max, int *reboot)
{
    const char *p = line;
    int ok, ok2;
    con_cap = out; con_cap_n = 0; con_cap_max = max; bb_asset_reboot_pending = 0;
    if (con_prefix(line, "bastat") || con_prefix(line, "bainit 1 ") || con_prefix(line, "baw ") ||
        con_prefix(line, "bacommit") || con_prefix(line, "barestart")) {
        if (!bb_asset_command(line))
            con_puts("refused\r\n");
    } else if (con_word(&p, "flr")) {
        const char *q = p;
        uint32_t a = con_num(&q, &ok), n = con_num(&q, &ok2);
        if (ok && ok2 && n >= 1u && n <= 128u && a >= 0xA0000u && a + n <= 0xDC000u)
            con_flr(p);
        else
            con_puts("refused\r\n");
    } else
        con_puts("refused\r\n");
    con_cap = 0;
    *reboot = bb_asset_reboot_pending;
    return con_cap_n;
}

#if FELUCCA_OTA
/* BB1 runs no stock editor, so this is the only handler of editor-framed SysEx: F0 7D 46 4C 50 <line> F7
 * (0x50 = command 80). Any other frame stays for ota_service(). Reply: F0 7D 46 4C 50 <console text> F7. */
static void bb_asset_midi_service(void)
{
    static uint8_t reply[5u + 600u + 1u] __attribute__((section(".pool")));
    const uint8_t *p;
    uint32_t n, i, k;
    char line[128];
    int reboot = 0;
    if (!ota_frame_get(&p, &n) || n < 7u || p[0] != 0x7Du || p[1] != 0x46u || p[2] != 0x4Cu || p[3] != 80u)
        return;
    for (i = 4, k = 0; i < n && k < sizeof line - 1u; i++, k++) {
        if (p[i] < 32u || p[i] > 126u) { k = 0; break; }      /* printable ASCII only */
        line[k] = (char)p[i];
    }
    line[k] = 0;
    ota_frame_done();
    if (k < 3u || i < n)
        return;                                           /* malformed or too long: no reply */
    reply[0] = 0xF0; reply[1] = 0x7D; reply[2] = 0x46; reply[3] = 0x4C; reply[4] = 80u;
    k = con_tunnel(line, (char *)reply + 5, 600u, &reboot);
    for (i = 0; i < k; i++)
        reply[5u + i] &= 0x7Fu;
    reply[5u + k] = 0xF7;
    ota_wire_send(reply, 6u + k);
    if (reboot) {
        fm1_delay_ms(20);
        fm1_reboot();
    }
}
#endif
static int bb_remote_command(const char *p);

static void con_exec(const char *p)
{
    if(bb_remote_command(p))return;
    if(bb_asset_command(p))return;
    if (con_word(&p, "help") || con_word(&p, "?"))
#if FELUCCA_TRACKS
        con_puts("status  dbg  bbdiag  kitdiag  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\r\n");
#else
        con_puts("status  dbg  bbdiag  kitdiag  inp  crash  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\r\n");
#endif
    else if (con_word(&p, "c1probe")) {
        bb_diag_counter("c1_probe_status",bb_c1_report.status);
        bb_diag_counter("c1_probe_alive",bb_c1_report.alive);
        bb_diag_counter("c1_probe_jobs",bb_c1_report.verified_jobs);
        bb_diag_counter("c1_probe_start_ticks",bb_c1_report.start_ticks);
        bb_diag_counter("c1_probe_job_max_ticks",bb_c1_report.max_job_ticks);
        bb_diag_counter("c1_probe_trace",bb_c1_report.trace);
        bb_diag_counter("c1_probe_stack_bytes",bb_c1_report.stack_bytes);
        bb_diag_counter("c1_probe_held",bb_c1_report.held);
        bb_diag_counter("c1_dsp_status",bb_c1_dsp_report.status);
        bb_diag_counter("c1_dsp_float_jobs",bb_c1_dsp_report.float_jobs);
        bb_diag_counter("c1_dsp_drum_jobs",bb_c1_dsp_report.drum_jobs);
        bb_diag_counter("c1_dsp_float_max_ticks",bb_c1_dsp_report.float_max_ticks);
        bb_diag_counter("c1_dsp_drum_max_ticks",bb_c1_dsp_report.drum_max_ticks);
        bb_diag_counter("c1_dsp_cpu0_max_ticks",bb_c1_dsp_report.cpu0_max_ticks);
        bb_diag_counter("c1_dsp_exact_cases",bb_c1_dsp_report.exact_cases);
        bb_diag_counter("c1_live_ready",bb_c1_live.ready);
        bb_diag_counter("c1_live_failed",bb_c1_live.failed);
        bb_diag_counter("c1_live_busy",bb_c1_live.busy);
        bb_diag_counter("c1_live_jobs",bb_c1_live.jobs);
        bb_diag_counter("c1_live_wait_max_ticks",bb_c1_live.wait_max_ticks);
        bb_diag_counter("c1_live_held",bb_c1_live.held);
        bb_diag_counter("c1_live_selected",bb_c1_live.selected);
        bb_diag_counter("c1_live_frames",bb_c1_live.frames);
        bb_diag_counter("c1_live_ticket",bb_c1_live.ticket);
        bb_diag_counter("c1_live_service_ticks",bb_c1_live.service_ticks);
        bb_diag_counter("c1_live_wait_ticks",bb_c1_live.wait_ticks);
        bb_diag_counter("c1_live_active_mask",bb_c1_live.active_mask);
        bb_diag_counter("c1_live_fail_ticket",bb_c1_live.fail_ticket);
        bb_diag_counter("c1_live_fail_done",bb_c1_live.fail_done);
        bb_diag_counter("c1_live_fail_bad",bb_c1_live.fail_bad);
        bb_diag_counter("c1_live_fail_frames",bb_c1_live.fail_frames);
        bb_diag_counter("c1_live_fail_selected",bb_c1_live.fail_selected);
        bb_diag_counter("c1_live_fail_mask",bb_c1_live.fail_mask);
        /* Only interpret this phase after the existing verified hold. */
        bb_diag_counter("c1_live_fail_trace",bb_c1_live.failed && bb_c1_live.held ? bb_c1_trace : 0u);
        bb_first_fault_context first_view={0};
        if(bb_first_fault.valid) {
            __asm__ volatile("" ::: "memory");
            first_view=bb_first_fault;
        }
        bb_diag_counter("firstfault_version",1u);
        bb_diag_counter("firstfault_valid",first_view.valid);
        bb_diag_counter("firstfault_reason",first_view.reason);
        bb_diag_counter("firstfault_stage",first_view.stage);
        bb_diag_counter("firstfault_page",first_view.page);
        bb_diag_counter("firstfault_home",first_view.home);
        bb_diag_counter("firstfault_frame_low",first_view.frame_low);
        bb_diag_counter("firstfault_frame_high",first_view.frame_high);
        bb_diag_counter("firstfault_selected",first_view.selected);
        bb_diag_counter("firstfault_ticket",first_view.ticket);
        bb_diag_counter("firstfault_applied",first_view.applied);
        bb_diag_counter("firstfault_queued",first_view.queued);
        bb_diag_counter("c1_phase",bb_c1_phase);
        bb_diag_counter("c1_trio_bound",bb_c1_trio_bound);
        bb_diag_counter("c1_arm_con",bb_c1_arm_con);
        bb_diag_counter("c1_arm_bad",bb_c1_arm_bad);
        bb_diag_counter("c1_arm_job",bb_c1_arm_job);
        bb_diag_counter("c1_arm_done",bb_c1_arm_done);
    }
    else if (con_word(&p, "densecost")) {
#include "dense_cost_console.inc"
    }
    else if (con_word(&p, "voicecost")) {
#include "voice_cost_console.inc"
    }
    else if (con_word(&p, "privcost")) {
        bb_diag_counter("privcost_samples",bb_private_cost.samples);
        bb_diag_counter("privcost_errors",bb_private_cost.errors);
        bb_diag_counter("privcost_enabled",bb_private_cost_enabled);
        bb_diag_counter("privcost_calibration_min",bb_private_cost.calibration_min);
        bb_diag_counter("privcost_calibration_max",bb_private_cost.calibration_max);
        bb_diag_counter("privcost_calibration_count",bb_private_cost.calibration_count);
        bb_diag_counter("privcost_input_min",bb_private_cost.minimum[0]);
        bb_diag_counter("privcost_input_max",bb_private_cost.maximum[0]);
        bb_diag_counter("privcost_input_sum",bb_private_cost.sum[0]);
        bb_diag_counter("privcost_groups_min",bb_private_cost.minimum[1]);
        bb_diag_counter("privcost_groups_max",bb_private_cost.maximum[1]);
        bb_diag_counter("privcost_groups_sum",bb_private_cost.sum[1]);
        bb_diag_counter("privcost_clear_min",bb_private_cost.minimum[2]);
        bb_diag_counter("privcost_clear_max",bb_private_cost.maximum[2]);
        bb_diag_counter("privcost_clear_sum",bb_private_cost.sum[2]);
        bb_diag_counter("privcost_render_min",bb_private_cost.minimum[3]);
        bb_diag_counter("privcost_render_max",bb_private_cost.maximum[3]);
        bb_diag_counter("privcost_render_sum",bb_private_cost.sum[3]);
        bb_diag_counter("privcost_output_min",bb_private_cost.minimum[4]);
        bb_diag_counter("privcost_output_max",bb_private_cost.maximum[4]);
        bb_diag_counter("privcost_output_sum",bb_private_cost.sum[4]);
        bb_diag_counter("privcost_failed_record_ticket",bb_private_cost.failed_record_ticket);
        bb_diag_counter("privcost_failed_record_mask",bb_private_cost.failed_record_mask);
    }
    else if (con_word(&p, "privrender")) {
        bb_diag_counter("privprofile_enabled",BB_AUDIO_PROFILE);
        /* Supplemental v2, kind9 owner diagnostic: same five slots/RAM.
         * 0 prepare,1 local synthesis,2 collect/validate,3 convert,4 copy.
         * Wall-time observer probes are not exclusive CPU utilization. */
        /* Sample v3:0 preparation+synthA,1 synthB,2 sample buses.
         * Owner v2 keeps its original scope definitions. */
        bb_diag_counter("privattr_version",BB_SAMPLE_HOST_INTEGRATION?3:2);
        bb_diag_counter("privattr_count",BB_ATTR_COUNT);
        bb_diag_counter("privattr_valid",bb_private_peak.valid && bb_attr_complete(&bb_private_peak_attribution));
        bb_diag_counter("privattr_errors",bb_private_peak_attribution.errors);
        bb_diag_counter("privattr_block",bb_private_peak.block);
        bb_diag_counter("privattr_ticks_0",bb_private_peak_attribution.cost[0].ticks);
        bb_diag_counter("privattr_calls_0",bb_private_peak_attribution.cost[0].calls);
        bb_diag_counter("privattr_frames_0",bb_private_peak_attribution.cost[0].frames);
        bb_diag_counter("privattr_ticks_1",bb_private_peak_attribution.cost[1].ticks);
        bb_diag_counter("privattr_calls_1",bb_private_peak_attribution.cost[1].calls);
        bb_diag_counter("privattr_frames_1",bb_private_peak_attribution.cost[1].frames);
        bb_diag_counter("privattr_ticks_2",bb_private_peak_attribution.cost[2].ticks);
        bb_diag_counter("privattr_calls_2",bb_private_peak_attribution.cost[2].calls);
        bb_diag_counter("privattr_frames_2",bb_private_peak_attribution.cost[2].frames);
        bb_diag_counter("privattr_ticks_3",bb_private_peak_attribution.cost[3].ticks);
        bb_diag_counter("privattr_calls_3",bb_private_peak_attribution.cost[3].calls);
        bb_diag_counter("privattr_frames_3",bb_private_peak_attribution.cost[3].frames);
        bb_diag_counter("privattr_ticks_4",bb_private_peak_attribution.cost[4].ticks);
        bb_diag_counter("privattr_calls_4",bb_private_peak_attribution.cost[4].calls);
        bb_diag_counter("privattr_frames_4",bb_private_peak_attribution.cost[4].frames);
        bb_diag_counter("privpeak_version",bb_private_peak.version);
        bb_diag_counter("privpeak_valid",bb_private_peak.valid);
        bb_diag_counter("privpeak_block",bb_private_peak.block);
        bb_diag_counter("privpeak_frame",bb_private_peak.frame);
        bb_diag_counter("privpeak_elapsed",bb_private_peak.elapsed);
        bb_diag_counter("privpeak_stage_sum",bb_private_peak.stage_sum);
        bb_diag_counter("privpeak_hits_a",bb_private_peak.hits_a);
        bb_diag_counter("privpeak_hits_b",bb_private_peak.hits_b);
        bb_diag_counter("privpeak_next_a",bb_private_peak.next_a);
        bb_diag_counter("privpeak_next_b",bb_private_peak.next_b);
        bb_diag_counter("privpeak_next_dillo",bb_private_peak.next_dillo);
        bb_diag_counter("privpeak_next_telegram",bb_private_peak.next_telegram);
        bb_diag_counter("privpeak_worker_service",bb_private_peak.worker_service);
        bb_diag_counter("privpeak_worker_wait",bb_private_peak.worker_wait);
        bb_diag_counter("privpeak_ticks_0",bb_private_peak.ticks[0]);
        bb_diag_counter("privpeak_calls_0",bb_private_peak.calls[0]);
        bb_diag_counter("privpeak_frames_0",bb_private_peak.frames[0]);
        bb_diag_counter("privpeak_ticks_1",bb_private_peak.ticks[1]);
        bb_diag_counter("privpeak_calls_1",bb_private_peak.calls[1]);
        bb_diag_counter("privpeak_frames_1",bb_private_peak.frames[1]);
        bb_diag_counter("privpeak_ticks_2",bb_private_peak.ticks[2]);
        bb_diag_counter("privpeak_calls_2",bb_private_peak.calls[2]);
        bb_diag_counter("privpeak_frames_2",bb_private_peak.frames[2]);
        bb_diag_counter("privpeak_ticks_3",bb_private_peak.ticks[3]);
        bb_diag_counter("privpeak_calls_3",bb_private_peak.calls[3]);
        bb_diag_counter("privpeak_frames_3",bb_private_peak.frames[3]);
        bb_diag_counter("privpeak_ticks_4",bb_private_peak.ticks[4]);
        bb_diag_counter("privpeak_calls_4",bb_private_peak.calls[4]);
        bb_diag_counter("privpeak_frames_4",bb_private_peak.frames[4]);
        bb_diag_counter("privpeak_ticks_5",bb_private_peak.ticks[5]);
        bb_diag_counter("privpeak_calls_5",bb_private_peak.calls[5]);
        bb_diag_counter("privpeak_frames_5",bb_private_peak.frames[5]);

        bb_diag_counter("privparity_version",bb_retained_parity.version);
        bb_diag_counter("privparity_status",bb_retained_parity.status);
        bb_diag_counter("privparity_cases",bb_retained_parity.cases);
        bb_diag_counter("privparity_bytes",bb_retained_parity.bytes);
        bb_diag_counter("privparity_jobs",bb_retained_parity.jobs);
        bb_diag_counter("privparity_failed_case",bb_retained_parity.failed_case);
        bb_diag_counter("privparity_worker_reason",bb_retained_parity.worker_reason);
        bb_diag_counter("privrender_send_blocks",bb_private_render_result.send_blocks);
        bb_diag_counter("privrender_wet_blocks",bb_private_render_result.wet_blocks);
        bb_diag_counter("privrender_drum_b_hits",bb_private_render_result.drum_b_hits);
        bb_diag_counter("privrender_callback_faults",bb_private_render_result.callback_faults);
        bb_diag_counter("privrender_status",bb_private_render_result.status);
        bb_diag_counter("privrender_version",bb_private_render_result.version);
        bb_diag_counter("privrender_kind",bb_private_render_result.kind);
        bb_diag_counter("privrender_completed_blocks",bb_private_render_result.completed_blocks);
        bb_diag_counter("privrender_render_ticks_sum",bb_private_render_result.render_ticks_sum);
        bb_diag_counter("privrender_physical_jobs",bb_private_render_result.physical_jobs);
        bb_diag_counter("privrender_logical_spans",bb_private_render_result.logical_spans);
        bb_diag_counter("privrender_min_ticks",bb_private_render_result.min_ticks);
        bb_diag_counter("privrender_blocks",bb_private_render_result.blocks);
        bb_diag_counter("privrender_ticket",bb_private_render_result.ticket);
        bb_diag_counter("privrender_applied",bb_private_render_result.applied);
        bb_diag_counter("privrender_max_ticks",bb_private_render_result.max_ticks);
        bb_diag_counter("privrender_total_ticks",bb_private_render_result.total_ticks);
        bb_diag_counter("privrender_worker_reason",bb_private_render_result.worker_reason);
        bb_diag_counter("privrender_worker_frames",bb_private_render_result.worker_frames);
        bb_diag_counter("privrender_worker_trace",bb_private_render_result.worker_trace);
        bb_diag_counter("privrender_source_error",bb_private_render_result.source_error);
        bb_diag_counter("privrender_transition_ticket",bb_private_render_result.transition_ticket);
        bb_diag_counter("privrender_transition_applied",bb_private_render_result.transition_applied);
        bb_diag_counter("privrender_transition_frame",bb_private_render_result.transition_frame);
    }
    else if (con_word(&p, "c1cost")) {
        /* Completed instrumentation retired; retained package has records. */
        bb_diag_counter("c1_cost_available",0u);
    }
    else if (con_word(&p, "bbdiag"))
        bb_con_diagnostics();
    else if (con_word(&p, "kitdiag")) {
#if BB_SAMPLE_HOST_INTEGRATION
        bb_con_kitdiag();
#else
        con_puts("kitdiag unavailable\r\n");
#endif
    }
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "inp"))
        con_inp();
#if FELUCCA_TRACKS
    else if (con_word(&p, "params"))
        con_params();
#endif
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

#include "bb_recovery_console.inc"

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nFelucca ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
