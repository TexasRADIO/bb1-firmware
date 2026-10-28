/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the
 * audio ISR: per half buffer, blocks of CTL samples: mix_block (fx.c: events ->
 * each synth part -> dist -> level / pan -> sends -> buses -> master) -> USB audio (uac_tap) -> the metronome's
 * click (click.c, the DAC only) -> 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#include "bb_target_timing.h"
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling (M0f ran clean at -18 dBFS) */

#define BB_DAC_SCALE 256 /* Q15 to24-bit; explicit full comparison policy */

#if HALF_FRAMES > 128u
/* Same addressable SRAM, explicit pool allocation. A larger DMA buffer must
 * not eat the small-variable RAM reserve; native resource gate still applies. */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4),section(".pool")));
#else
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
#endif

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    bb_target_audio(out, n);
#if FELUCCA_UAC
    uac_tap(out, n);                                    /* the USB audio input: the same master output */
#endif
    if (fx_usb_fixed)                                   /* USB LEVEL FIXED: USB took the full level, the DAC */
        usb_fixed_dac(out, n);                          /* (speaker, headphones) gets MASTER's (fx.c) */
    if (!scope_mix)                                     /* the HOME scope (SCOPE OUT): the music, before the click */
        for (i = 1; i < n; i += 2u)                     /* (MIX: fx.c mix_block took it) */
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
    click_render(out, n);                               /* the metronome (click.c): the DAC only, not USB */
    for (i = 0; i < n; i++) {
        out[2u * i] *= BB_DAC_SCALE;
        out[2u * i + 1u] *= BB_DAC_SCALE;
    }
}

/* Big Beat release: the stock overload shedder (shed_voice) is removed. It
 * could only fade stock Felucca voices (trk[].v[]); in this image nothing can
 * start one (voice_start is not linked and no live code writes v->active
 * non-zero), so it never found a voice and only kept the stock engine table
 * (ENGINES -> every eng_*.c renderer and eng_mem) alive through trk_vmode.
 * Proof: claude-code/reduction/DELETION-MAP.md item D1. shed_count stays for
 * the console's unchanged "voices_shed" line; it was and remains 0. */
static uint32_t shed_count;

/* end of a half: all = its ticks, TIMER5 nested in it included -> the render alone in us. The deadline sees
 * both (out of line: keeps the ISR's render loops as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all, uint32_t nested)
{
    return (all - (nested > all ? all : nested)) / FM1_TICKS_PER_US;
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us, elapsed_ticks, nested_ticks;
        bb_target_timing_entry(t0);
        int32_t *o = &abuf[half * HALF_WORDS];
#if FELUCCA_UAC
        uac_render_start();
#endif
#if BB_AUDIO_NATIVE_HALF
#if (HALF_FRAMES != 128 && HALF_FRAMES != 256) || CTL != 32
#error "Half render requires qualified DMA halves and the preserved 128/32 DSP cadence"
#endif
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#if BB_AUDIO_NATIVE_HALF
            /* Restart the128-frame cache for EACH render quantum. Resetting
             * only once per DMA half would route the second half through the
             * old32-frame renderer and defeat the scheduling comparison. */
            if((b&127u)==0)bb_target_half_begin();
#endif
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        audio_halves++;
        /* Snapshot nested work before the endpoint: an intervening TIMER5 tick is
         * included in elapsed but not nested, never the reverse (no underflow). */
        nested_ticks = t5_nested_ticks;
        elapsed_ticks = fm1_ticks() - t0;
        bb_target_timing_half(elapsed_ticks, nested_ticks, HALF_FRAMES * DAC_TICKS);
        bb_target_timing_boundary(elapsed_ticks, bb_audio_boundary_flags);   /* P1b boundary split */
        bb_audio_boundary_flags = 0u;
        us = shed_check(elapsed_ticks, nested_ticks);
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half) {
            felucca_dbg.late++;                     /* the DMA moved on while we rendered */
            bb_target_timing_dma_miss();
        }
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
