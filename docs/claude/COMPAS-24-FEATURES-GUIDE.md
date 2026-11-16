# Compás — BB1 2.4 Feature Applicability Guide

**16 November 2026 · Claude (review)**
**Revised after Codex review b6f120a — fixes all six findings**

---

## Summary

Three BB1 2.4 features reviewed for Compás applicability. One applies with careful adaptation, one applies as shared code after fixes, one doesn't apply.

---

## 1. Conditional Steps — DOES NOT APPLY (already solved differently)

**BB1's approach:** Per-event condition bits (FILL, FIRST, EVERY_2, EVERY_4, NOT_FILL) stored in a 3-bit sidecar on the `bb_phrase` event list, behind `BB_WITH_STEP_CONDITIONS`.

**Why it doesn't fit Compás:** Compás uses a `Study` / `Part` architecture with six roles (Low, Skin, Hand, Shake, Scrape, Lead — not generic bass/percussion/piano/guide). Each role has three separate 32-step arrays:

```c
typedef struct {
    uint8_t sound;
    uint8_t base[32];   /* normal pattern  */
    uint8_t lift[32];   /* variation        */
    uint8_t fill[32];   /* fill pattern     */
} Part;
```

This is not equivalent to BB1's per-event FIRST/EVERY_2/EVERY_4 conditions — those control per-hit per-loop variation, while Compás switches between whole alternative arrays. Base/lift/fill provides section-level variation; BB1 conditions provide hit-level variation within a single pattern. The difference matters and they should not be described as "already implemented" versions of each other.

Adding FIRST/EVERY_2/EVERY_4 to Compás would require per-step condition bits on 32-step arrays for 6 roles. At 3 bits per step × 32 steps = 12 bytes per role × 6 roles = 72 bytes per Study. With `intro[]` arrays (32 B × 6 roles = 192 B per Study before alignment). These are not trivial additions to `score.inc` compiled data.

**Recommendation: defer.** Compás's existing variation system serves its musical needs. Per-hit conditions are a Compás V2 conversation after measuring the score-data and RAM impact.

---

## 2. Quantised Fills — NEEDS CAREFUL SPECIFICATION

**BB1's approach:** Fill trigger quantises to the next beat or half-beat boundary.

**Current Compás behaviour (actual, from engine.c):** `CP_FILL` does NOT toggle immediately as the previous guide stated. The real implementation:

1. `cp_command(CP_FILL, 1, 0)` rejects stopped/count-in/busy states
2. It arms `s.fill = 1` and records `s.fill_phrase`
3. `cp_render` activates the fill pattern at `pos == study.steps - 4` (four steps before the phrase wraps)
4. The fill clears on the next phrase wrap
5. Late requests that arrive during the final four steps target the following phrase

**This is already quantised** — to the final beat of the current phrase, not to the next arbitrary beat. It has specific phrase-boundary, ARP, and part-cue semantics.

**What the previous guide got wrong:**
- Quoted a one-line `s.fill = (uint8_t)a; return CP_OK;` handler that does not exist
- Proposed adding a `fill_armed` flag when arming already exists
- Claimed ~20 B code cost without measurement
- Did not account for REBAJADA, 727/Kumbia overlay, MIDI/Sideman, or cue-boundary interactions

**Revised recommendation:** The existing fill timing may already be correct for Compás's musical needs. If a different quantisation point is desired (e.g. next beat rather than final beat), the change must:

1. Preserve the stopped/count-in/busy rejection
2. Preserve the ARP and part-cue interaction
3. Define behaviour for requests that arrive at boundary (within 1 step)
4. Test with REBAJADA speed, 727 overlay, MIDI clock, and Sideman observation
5. Measure actual code and RAM cost on pi32v2 under the 2.901 ms deadline

**Priority: investigate, not implement blindly.** The existing fill timing may be the right design.

---

## 3. Vinyl Brake — APPLIES AFTER FIXES (shared code)

**What it is:** A tempo-deceleration performance gesture. Press-and-hold decelerates the master clock; release snaps back.

**Fixes applied to `bb_vinyl_brake.h/c` after review:**

1. **Removed `bb_core.h` dependency.** The module now includes only `<stdint.h>` and `<string.h>`. No platform headers. Standalone compile confirmed.

2. **Switched from milliseconds to microseconds.** At 128 frames / 44.1 kHz, a block is 2902 µs — truncating to 2 ms integer would stretch an 800 ms gesture to ~1.16 s. Microsecond timing preserves fractional block duration.

3. **Fixed return-ramp interpolation.** The release-start BPM is frozen in a dedicated `release_bpm` field. The return ramp interpolates `release_bpm → original_bpm` using total elapsed time — deterministic regardless of block size or callback partition.

4. **Added `bb_vinyl_brake_cancel()`.** On transport STOP, song cue change, or external event, the brake cancels immediately with no ramp. The caller restores `original_bpm` to the clock.

5. **Fixed `stop_at_zero` — now has distinct behaviour.** When `stop_at_zero = 1`, the brake holds at zero until release (then ramps back). When `stop_at_zero = 0`, the brake auto-returns from zero without waiting for release.

6. **Struct size: measure with `sizeof`, not assume.** Previous guide claimed 12 B; Codex measured 14 B on host. The actual size is platform-dependent (pi32v2 alignment may differ from x86). The header now documents this explicitly.

**Integration model for Compás (corrected):**

The previous guide proposed directly writing to `s.p.bpm` and `s.threshold` from panel.c. This is wrong:

- `engine.c`'s `s` is static/private — panel.c cannot access it
- `CP_BPM` accepts only 60..180 — the brake returns values below that range
- `s.threshold = s.sr * 60` is constant, not BPM-dependent — it was misidentified

The correct integration is a **transient transport-rate overlay** owned by the audio path:

```c
/* In board/panel.c — the pad gesture: */
void cp_brake_press(void) {
    unsigned bpm = cp_status(CP_TEMPO);
    bb_vinyl_brake_engage(&brake, bpm);
}
void cp_brake_release(void) {
    bb_vinyl_brake_release(&brake);
}

/* In the audio owner's block callback (NOT panel.c):
 * The brake returns a transient speed that bypasses cp_command(CP_BPM)
 * validation. The audio owner applies it directly to its phase
 * accumulator, NOT to the saved s.p.bpm. The saved tempo is restored
 * on cancel/completion. */
```

The exact audio-owner integration requires deciding:
- How the phase accumulator responds to sub-60 BPM speeds
- Whether held notes and effect tails continue at the slowed rate or at saved tempo
- USB MIDI clock behaviour during the brake
- Sideman observation during the brake
- Interaction with STOP, song cue, and count-in

**Cost: measure on target after integration.** The module is ~120 lines of integer arithmetic. Flash, RAM, stack and IRQ cost must be measured against the 2.901 ms deadline at 128/44100, with maximum FX/Kumbia/dense grooves plus UI/MIDI load. The header makes no performance claim.

**Musical context:** In cumbia and salsa performance, the "freno" (brake) is a known live move — the band decelerates to a dramatic stop before a section change. This module provides the tempo curve; the audio design for record-stop sound (pitch-down, deceleration of running audio) is a separate requirement that this module does not address. The brake changes event tempo only, not rendered audio speed or pitch.

**Recommendation: prototype on a development candidate.** Wire the brake to a pad gesture and measure. Do not ship without the audio-owner integration contract settled and tested under dense load.

---

## 4. Live Stutter — DOES NOT APPLY (defer to V2)

The stutter sketch from the BB1 2.4 design is a design outline, not a working retrigger path. It increments a step counter per step callback and cannot generate repeats faster than the step rate. A proper stutter needs:

- A sample/clock subdivision scheduler (sub-step timing)
- Defined behaviour for phrase phase, live notes, accent/slide, release and cues
- External clock interaction
- Measured retrigger cost (voice re-initialisation is not free)

At Compás's 2.901 ms deadline with six synthesised voices, the retrigger cost must be profiled before committing. Deferring to V2.

---

## Adoption summary

| Feature | Compás | Status | Next step |
|---------|--------|--------|-----------|
| Conditional steps | No (different architecture) | Defer V2 | Measure score-data impact |
| Quantised fills | Investigate | Existing timing may be correct | Compare with desired musical behaviour |
| Vinyl brake | Yes (shared code, fixed) | Prototype | Wire, measure, settle audio-owner contract |
| Live stutter | No | Defer V2 | Needs subdivision scheduler + profiling |
