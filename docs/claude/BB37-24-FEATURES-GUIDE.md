# BB–37 Elite — BB1 2.4 Feature Applicability Guide

**16 November 2026 · Claude (review)**

---

## Summary

Three BB1 2.4 features reviewed for BB–37 applicability. All three apply, but conditional steps needs adaptation work.

---

## 1. Conditional Steps — APPLIES WITH SIGNIFICANT ADAPTATION

**BB1's approach:** The `bb_phrase` system with per-event condition sidecar, behind the `BB_WITH_STEP_CONDITIONS` compile flag. Six conditions evaluated per step.

**BB–37's current state:** The Elite uses `bb_core` (shared with BB1) which has `bb_drum_step` for drum patterns and `bb_note` for synth sequences. The Elite's `elite_set_drum_pattern()` and `elite_set_notes()` take these simpler structures, not `bb_phrase`.

However, `bb_core.h` is shared code. The Elite already has the `bb_drum_step` type:

```c
typedef struct { uint16_t hits, accents; uint32_t ratchets; } bb_drum_step;
```

The condition system lives in `bb_phrase` (the rich event-list representation), not in `bb_drum_step` (the per-step bitmask representation). To bring conditional steps to the Elite, there are two paths:

### Path A: Promote Elite to `bb_phrase` (recommended for 2.4)

The Elite's Conductor already manages song parts with `elite_set_drum_pattern()`. If the Conductor adopts `bb_phrase` for its internal representation (as BB1's section system does), conditional steps come for free — the existing `bb_phrase_condition_play()` and `bb_phrase_index_hit_enabled()` evaluate the conditions.

This means the Elite's pattern storage switches from flat `bb_drum_step[64]` arrays to `bb_phrase` structs. The tradeoff: each `bb_phrase` is 340 bytes (with conditions) vs 640 bytes for a 64-step `bb_drum_step` array — it's actually smaller because `bb_phrase` packs events compactly.

```c
/* In elite.h, the drum pattern path would change from: */
int elite_set_drum_pattern(elite *, unsigned drum, const bb_drum_step *, unsigned length);

/* To: */
int elite_set_phrase(elite *, unsigned drum, const bb_phrase *);
```

The renderer then uses `bb_phrase_index_hit_enabled()` to evaluate conditions per step, same as BB1.

### Path B: Add conditions to `bb_drum_step` (simpler, less capable)

Add a condition byte per step alongside the existing hits/accents/ratchets:

```c
typedef struct {
    uint16_t hits, accents;
    uint32_t ratchets;
    uint8_t conditions[BB_VOICES]; /* 4 bits per voice would pack tighter */
} bb_drum_step_ext;
```

This is simpler but doesn't share the same code as BB1's proven implementation.

**Recommendation: Path A for the Elite's Conductor songs.** The `bb_phrase` system is battle-tested in BB1 and comes with serialization, validation and editor protocol support already built. The Elite benefits from sharing this codebase rather than diverging.

**Cost:** ~2 KB additional code for the phrase index integration. RAM is comparable or lower than the current flat arrays.

---

## 2. Quantised Fills — APPLIES DIRECTLY

**BB1's approach:** Fill trigger quantises to the next beat boundary before activating. The Conductor's `fill_active` flag gates per-step conditions.

**BB–37's Conductor:** The Elite already has a Conductor that manages song parts with cue-at-bar-boundary behaviour. Adding quantised fills follows the same path as BB1:

```c
/* In the Elite's Conductor step advance: */
if (fill_armed && step_is_beat_boundary(step, length)) {
    fill_active = 1;
    fill_armed = 0;
}
```

The 8 encoders on the SMK-37 Elite are ideal for fill control — one encoder or pad triggers the fill, and the quantisation ensures it lands musically.

**Cost:** Minimal — one flag and one comparison per step. If the Elite adopts `bb_phrase` (Path A above), the fill system comes for free as part of the condition evaluator.

**Recommendation: include alongside conditional steps.** They're the same system.

---

## 3. Vinyl Brake — APPLIES DIRECTLY

**BB1's approach:** `bb_vinyl_brake.h/c` — shared tempo-modulation gesture with no BB1-specific dependencies.

**How it maps to the Elite:** The Elite's `bb_core clock` holds the BPM. The brake modifies it exactly as in BB1:

```c
/* In the Elite's control path: */
#include "bb_vinyl_brake.h"

static bb_vinyl_brake brake;

/* Mapped to a pad gesture (e.g. hold pad 16): */
void elite_brake_press(void) {
    bb_vinyl_brake_engage(&brake, e->clock.bpm);
}
void elite_brake_release(void) {
    bb_vinyl_brake_release(&brake);
}

/* Per block: */
if (bb_vinyl_brake_active(&brake)) {
    unsigned new_bpm = bb_vinyl_brake_tick(&brake, block_ms);
    bb_set_tempo(&e->clock, new_bpm);
}
```

The vinyl brake is a natural fit for the Elite's performance-oriented design. The SMK-37's physical controls (pads and keys) map well to press-and-hold gestures.

**Cost:** Same as Compás — ~400 bytes code, 12 bytes RAM. Well within the Elite's audio budget (5.802 ms deadline at 256 frames, same as BB1).

**Recommendation: include in the next Elite update.** Drop `bb_vinyl_brake.h/c` into the build, wire to a pad.

---

## 4. Live Stutter — APPLIES DIRECTLY

**BB1's approach:** Freeze current step, retrigger at a selectable division.

**Why it works for the Elite:** The Elite renders drums via `drum808_t` or `drum_playback` and synths via `bb_dillo` / `bb_telegram`. Retriggering drums at a captured step is the same operation as BB1's — trigger the voice at the captured step's velocity. The synths (Telegram and Dillo) continue playing their held notes through the stutter.

The SMK-37's 8 encoders are perfect for rate control during stutter: hold a pad to engage stutter, turn an encoder to change the division.

```c
/* In the Elite's audio render path: */
typedef struct {
    uint8_t active, rate, captured_step;
    uint16_t sub_count, period;
} elite_stutter;

/* Per step advance: */
if (stutter.active) {
    if (++stutter.sub_count >= stutter.period) {
        stutter.sub_count = 0;
        elite_retrigger_step(e, stutter.captured_step);
    }
}
```

**Cost:** ~200 bytes code, 8 bytes RAM. No CPU increase — same number of voices, just retriggered.

**Recommendation: include in the next Elite update.** Natural fit for the SMK-37's pad-and-encoder workflow.

---

## Adoption summary

| Feature | BB–37 | Priority | Dependency |
|---------|-------|----------|------------|
| Conditional steps | Yes (via `bb_phrase` promotion) | High | Needs Conductor refactor |
| Quantised fills | Yes (comes with conditions) | High | Same as above |
| Vinyl brake | Yes (shared code) | Medium | None — drop in |
| Live stutter | Yes (retrigger drums) | Medium | None — small module |

**Recommended order:** Vinyl brake and stutter first (drop-in, no refactor). Then conditional steps + fills as part of a Conductor revision.
