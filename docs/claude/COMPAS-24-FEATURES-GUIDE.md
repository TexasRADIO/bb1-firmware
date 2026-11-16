# Compás — BB1 2.4 Feature Applicability Guide

**16 November 2026 · Claude (review)**
**For the Compás design thread**

---

## Summary

Three BB1 2.4 features reviewed for Compás applicability. One applies directly, one applies with adaptation, one doesn't apply.

---

## 1. Conditional Steps — DOES NOT APPLY (already solved differently)

**BB1's approach:** Per-event condition bits (FILL, FIRST, EVERY_2, EVERY_4, NOT_FILL) stored in a sidecar bitfield on the `bb_phrase` event list. Each drum hit carries its own condition.

**Why it doesn't fit Compás:** Compás uses a `Study` / `Part` architecture where fills are already a first-class separate array:

```c
typedef struct {
    uint8_t sound;
    uint8_t base[32];   /* normal pattern */
    uint8_t lift[32];   /* variation       */
    uint8_t fill[32];   /* fill pattern    */
} Part;
```

Each of the six roles (bass, percussion, piano, guide, etc.) has its own base, lift and fill patterns as separate 32-step arrays. The `CP_FILL` command (`s.fill` flag) already switches between base/lift and fill playback. This is architecturally equivalent to BB1's FILL/NOT_FILL conditions but designed differently — the fill isn't per-step metadata, it's a whole alternative pattern per role.

**What Compás could adopt instead:**

The FIRST and EVERY_2/EVERY_4 concepts — intro variation and loop-to-loop alternation — could work in Compás as **study-level modifiers** rather than per-step conditions:

```c
/* Proposed addition to the Study struct: */
typedef struct {
    uint8_t steps, bpm, break_mask;
    uint8_t intro_mask;     /* NEW: roles that play differently on first pass */
    uint8_t alt_mask;       /* NEW: roles that alternate every other pass    */
    Part part[CP_ROLES];
    uint8_t intro[CP_ROLES][32];  /* NEW: first-pass pattern per role */
} Study;
```

But this doubles the Study size and the library data (`score.inc`). **Recommendation: defer.** Compás's existing base/lift/fill already gives musical variation. Adding FIRST/ALT is a Compás V2 conversation.

---

## 2. Quantised Fills — APPLIES WITH ADAPTATION

**BB1's approach:** Fill trigger quantises to the next beat or half-beat boundary before activating.

**Current Compás behaviour:** `CP_FILL` toggles `s.fill` immediately. The pattern switches at the next step, not the next beat.

**What to change:**

```c
/* In engine.c, cp_command CP_FILL handler: */
/* BEFORE (current): */
case CP_FILL: s.fill = (uint8_t)a; return CP_OK;

/* AFTER (quantised): */
case CP_FILL:
    if (a) {
        /* Arm the fill; activate at the next downbeat */
        s.fill_armed = 1;
    } else {
        s.fill = 0;
        s.fill_armed = 0;
    }
    return CP_OK;
```

Then in the render loop, when the sequencer advances to a beat boundary:

```c
/* In the step-advance section of cp_render: */
if (s.fill_armed && (s.pos % beats_per_bar == 0)) {
    s.fill = 1;
    s.fill_armed = 0;
}
```

Where `beats_per_bar` depends on the study's step count (typically 4 beats for a 16-step or 32-step pattern).

**Cost:** One new `uint8_t fill_armed` field in the engine state. ~20 bytes of code. No RAM increase beyond the flag.

**Musical justification:** Latin music fill transitions are often on the clave — quantising to the beat aligns with how a live percussionist would enter a fill. The fill should land on a strong beat (1 or 3 in a 4/4 bar, or on the first beat of a clave cycle).

**Recommendation: include in Compás 1.3.** Low cost, immediate musical improvement.

---

## 3. Vinyl Brake — APPLIES DIRECTLY

**BB1's approach:** `bb_vinyl_brake.h/c` — a tempo-modulation gesture with configurable deceleration curve, snap-back on release, and stop-at-zero option.

**How it maps to Compás:** Compás stores its BPM in `s.p.bpm` and sets the step threshold via `s.threshold = s.sr * 60`. The brake would operate identically:

```c
/* In board/panel.c or the control path: */
#include "bb_vinyl_brake.h"

static bb_vinyl_brake brake;

/* On designated pad press: */
void cp_brake_press(void) {
    bb_vinyl_brake_engage(&brake, s.p.bpm);
}

/* On pad release: */
void cp_brake_release(void) {
    bb_vinyl_brake_release(&brake);
}

/* Per audio block (in the main render call): */
if (bb_vinyl_brake_active(&brake)) {
    unsigned new_bpm = bb_vinyl_brake_tick(&brake, block_ms);
    if (new_bpm != s.p.bpm) {
        s.p.bpm = (uint8_t)new_bpm;
        s.threshold = s.sr * 60;  /* recalc step threshold */
    }
}
```

**The vinyl brake code (`bb_vinyl_brake.h` / `bb_vinyl_brake.c`) is shared — it has no BB1 dependencies.** It operates on unsigned BPM values and millisecond ticks. Compás can include it directly.

**Musical justification:** The vinyl brake is a dramatic performance gesture that works in any tempo-based music. In cumbia and salsa, the "freno" (brake) is a known live performance move — the band decelerates to a dramatic stop before a section change. The gesture maps naturally.

**Cost:** ~400 bytes of code, 12 bytes of RAM (the `bb_vinyl_brake` struct). The deceleration curve is integer-only arithmetic, well within Compás's tighter 2.901 ms audio deadline.

**Recommendation: include in Compás 1.3.** Drop `bb_vinyl_brake.h` and `bb_vinyl_brake.c` into the Compás build, wire to a pad gesture.

---

## 4. Live Stutter — DOES NOT APPLY DIRECTLY

**BB1's approach:** Freeze the current step and retrigger at a selectable rate (1/4 through 1/32).

**Why it's tricky for Compás:** Compás renders six voices per step, each with its own synthesis model (not sample playback). "Retriggering a step" means re-synthesising all six roles at the captured step, which is heavier than BB1's sample re-trigger. The 2.901 ms deadline at 128-frame IRQ is tighter.

**Recommendation: defer to Compás V2.** The vinyl brake covers the "dramatic live gesture" need for now. Stutter would need careful profiling against the tighter deadline.

---

## Adoption summary

| Feature | Compás | Priority | Cost |
|---------|--------|----------|------|
| Conditional steps | No (already has base/lift/fill) | — | — |
| Quantised fills | Yes, adapt `CP_FILL` | 1.3 | ~20 B code, 1 B RAM |
| Vinyl brake | Yes, shared code | 1.3 | ~400 B code, 12 B RAM |
| Live stutter | Defer to V2 | — | Needs profiling |
