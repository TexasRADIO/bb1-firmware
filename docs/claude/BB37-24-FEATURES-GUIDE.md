# BB–37 Elite — BB1 2.4 Feature Applicability Guide

**16 November 2026 · Claude (review)**
**Revised after Codex review (shared-gestures-review-20261116) — fixes all six findings**

---

## Summary

Three BB1 2.4 features reviewed for BB–37 applicability. All are worth prototyping; none is a drop-in. The previous guide overstated readiness.

---

## 1. Conditional Steps — APPLIES, NEEDS PROPER ADAPTATION

**BB1's approach:** The `bb_phrase` system with per-event condition sidecar, behind `BB_WITH_STEP_CONDITIONS`. Six conditions evaluated per step.

**BB–37's current state:** The Elite uses `bb_core` (shared with BB1) which has `bb_drum_step`:

```c
typedef struct { uint16_t hits, accents; uint32_t ratchets; } bb_drum_step;
```

This is 8 bytes per step. A 64-step pattern is 512 bytes (not 640 as previously stated — that was wrong). The `bb_drum_step` in the Elite's retained source has no `ratchets` member either, so the actual Elite type may be smaller still.

**What the previous guide got wrong:**
- Claimed `bb_phrase` (340 B) saves RAM vs `bb_drum_step[64]` (claimed 640 B). The actual `bb_drum_step` array is 512 B; the comparison was wrong. And `bb_phrase` carries different information (event lists vs step bitmasks) — it's not a like-for-like replacement.
- Proposed "just enable the flag" — BB1's `BB_WITH_STEP_CONDITIONS` flag gates a complete phrase-based system that the Elite doesn't use. Enabling it requires the Elite's Conductor to adopt the phrase architecture first.
- Stated 5.802 ms audio deadline. Codex reports the Elite's working acceptance limit is 5 ms. The XIP headroom on Build 826 is 112 bytes; after editor repair, 48 bytes. A 400-byte addition does not fit.

**Proper adaptation path:**

The Elite needs its own condition representation fitted to its existing `bb_drum_step` or its actual pattern storage, within actual measured capacity. Options:

**A. Per-voice condition byte alongside `bb_drum_step`:**
Add a condition byte per voice per step as a sidecar array, evaluated against a loop counter and fill flag. This keeps the existing pattern storage intact and adds the condition evaluation without adopting `bb_phrase`.

```c
/* Per-step condition sidecar, separate from bb_drum_step: */
uint8_t conditions[BB_STEPS][BB_VOICES]; /* 4 bits each, packed = 512 B */
/* Or packed: (BB_STEPS * BB_VOICES * 4 + 7) / 8 = 256 B */
```

**B. Extend `bb_drum_step` with a condition bitmask:**
Add condition bits per voice in the existing step struct. This changes the struct layout and affects all serialised patterns.

Either way, the work includes:
- Condition evaluation matching BB1's `bb_phrase_condition_play` semantics
- Loop counter in the Conductor
- Fill flag wired to a pad gesture
- Storage migration for existing saved setups (old patterns load with ALWAYS)
- Measured code/RAM cost against the 5 ms deadline and actual XIP headroom
- Dense pattern tests (all 16 voices, all conditions active)

**Recommendation: design first, then prototype on a development candidate.** This is a Conductor revision, not a flag flip. Existing saved setups must be preserved.

---

## 2. Quantised Fills — APPLIES, NEEDS SPECIFICATION

Quantised fills depend on condition steps (the FILL/NOT_FILL conditions gate which steps sound during a fill). The fill timing needs specification against the Elite's existing bar-cue ownership:

- When in the bar does the fill activate? (Next beat? Next bar? Immediately?)
- How does it interact with the Conductor's existing song-part cue system?
- Does it interact with held Telegram/Dillo notes?
- USB MIDI clock behaviour during fills

**Recommendation: specify alongside conditional steps.** They're the same system.

---

## 3. Vinyl Brake — APPLIES AFTER FIXES

The shared `bb_vinyl_brake.h/c` module has been revised (see Compás guide for the full fix list). The module is platform-independent and operates on `bb_core`'s BPM via `bb_set_tempo`.

**Integration challenge specific to Elite:**

`bb_set_tempo` accepts 30..300 BPM. The brake returns values below 30 and eventually 0. The Elite needs a transient transport-rate path that bypasses the tempo validator, applied by the audio owner, not by the UI. The saved song tempo must stay distinct from the transient brake speed.

Additional Elite-specific concerns:
- Held Telegram and Dillo keys during the brake (do they sustain? retrigger? release?)
- USB MIDI clock output during the brake (does it decelerate? hold? stop?)
- Interaction with the Conductor's part cues and auto-advance
- The brake changes event scheduling tempo, not rendered audio pitch — a vinyl-stop sound needs separate audio design

**Cost: measure on target.** The module is ~120 lines of integer arithmetic. Must fit within the Elite's 5 ms deadline and actual XIP headroom. Do not assume it fits from the absence of additional voices.

**Recommendation: prototype independently of the locked release.** Fix the clock contract first, then wire to a pad.

---

## 4. Live Stutter — NEEDS DESIGN WORK

**What the previous guide got wrong:**
- Presented a per-step counter sketch as if it were a working retrigger path. The sketch increments `sub_count` per step and cannot generate repeats faster than the step rate. A 1/32 stutter at 120 BPM is 16 Hz — the step rate at 16th notes is 8 Hz. The stutter needs a sub-step timing source.
- Claimed "no CPU increase — same number of voices, just retriggered." Voice re-initialisation has a cost (envelope restart, filter state reset). Repeated 808 kick triggers at 1/32 rate under full Telegram + Dillo + Band load must be measured, not assumed constant.
- Did not define behaviour for: phrase phase during stutter, held live Telegram/Dillo keys, accent and slide on retriggered steps, release from stutter (snap to current phase or rewound phase), cue transitions, external clock.

**What's needed for a proper stutter:**
1. A sample-clock subdivision scheduler that fires between steps
2. Captured state includes voice parameters, not just step index
3. Defined phase/position semantics on release
4. Measured retrigger cost under dense load at 5 ms deadline

**Recommendation: design the subdivision scheduler, then prototype.** The 8-encoder hardware is ideal for rate control, but the audio path needs real work.

---

## Adoption summary

| Feature | BB–37 | Status | Blocker |
|---------|-------|--------|---------|
| Conditional steps | Yes (own representation) | Design | Capacity, migration, measurement |
| Quantised fills | Yes (with conditions) | Design | Same as above |
| Vinyl brake | Yes (shared code, fixed) | Prototype | Clock contract, measurement |
| Live stutter | Yes (concept) | Design | Subdivision scheduler, measurement |

**Recommended order:** Settle the vinyl brake clock contract first (simplest module). Then design conditional steps + fills as a Conductor revision with measured capacity. Stutter last — it needs the most new audio-path work.

No released package, device or product branch was changed by this review.
