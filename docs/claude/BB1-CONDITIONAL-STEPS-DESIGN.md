# BB1 — Conditional Steps

**16 November 2026 · Claude (review)**
**Inspired by Matryoshka 1.17 — clean-room behavioural design only**

---

## 1. What conditional steps are

A conditional step fires only when its condition is true. The step stays in the pattern at all times; the condition controls whether it produces sound on this pass. This gives one pattern the variation of several without multiplying pattern storage.

---

## 2. Conditions for BB1

Six conditions, each with an inverse (12 values, fits in 4 bits):

| ID | Name | Symbol | Fires when |
|----|------|--------|-----------|
| 0 | ALWAYS | — | Every pass (default, no condition) |
| 1 | FILL | F | Inside a Conductor fill |
| 2 | !FILL | !F | Outside a fill (normal playback only) |
| 3 | FIRST | 1st | First pass of this pattern since it was cued |
| 4 | !FIRST | !1st | Every pass except the first |
| 5 | ALT-A | A | Odd-numbered passes (1, 3, 5…) |
| 6 | ALT-B | B | Even-numbered passes (2, 4, 6…) |
| 7 | PRE | ← | The step before an active step fires too |
| 8 | !PRE | !← | Only if no active step follows |
| 9 | X% | % | Probability — fires X% of the time |
| 10–15 | reserved | | Future use |

### Why these conditions

- **FILL/!FILL** ties directly to the quantized fills design (BB1-QUANTIZED-FILLS-STUTTER-DESIGN.md). Steps marked FILL are silent during normal playback and come alive only when the performer triggers a fill. Steps marked !FILL go silent during fills. This lets a single pattern contain both its main groove and its fill in one place.

- **FIRST/!FIRST** gives natural intros: a crash cymbal on beat 1 that plays only the first time a pattern loops, or a stripped-down first pass that opens up on subsequent loops.

- **ALT-A/ALT-B** alternate between two halves every loop. Classic use: open hi-hat every other pass, or a slight kick variation that keeps the groove alive over long loops.

- **PRE** adds ghost notes and flams that depend on what follows. If the next step has a snare, PRE adds a ghost hit before it. If the snare step is removed, the ghost disappears too.

- **X%** (probability) adds humanisation. A 50% hi-hat sometimes plays, sometimes doesn't. The percentage is stored as a 4-bit value (0–15 mapping to 0%–100% in ~7% steps). Random seed per bar for repeatability within the bar.

---

## 3. Storage

### Current step format

Each step in BB1's drum sequencer stores:

```c
typedef struct {
    uint8_t voice   : 4;  /* which voice */
    uint8_t accent  : 1;  /* accented */
    uint8_t ratchet : 2;  /* 0=off, 1=2×, 2=3×, 3=4× */
    uint8_t active  : 1;  /* step is on */
} bb_step_t;             /* 1 byte per step */
```

### Extended step format

Add a condition byte per step:

```c
typedef struct {
    uint8_t voice     : 4;
    uint8_t accent    : 1;
    uint8_t ratchet   : 2;
    uint8_t active    : 1;
    uint8_t condition : 4;  /* 0=ALWAYS, see table above */
    uint8_t cond_val  : 4;  /* parameter: probability % for X%, or 0 */
} bb_step_ext_t;            /* 2 bytes per step */
```

### RAM cost

- 64-step pattern × 16 voices × 2 bytes = 2,048 bytes per machine
- Both machines: 4,096 bytes = 4 KB
- Dillo steps (if extended): 64 steps × 2 bytes = 128 bytes
- **Total: ~4.2 KB RAM** for the working pattern

This is within budget. The AC791N has 300 KB SRAM; BB1 2.3 uses approximately 180 KB including sample buffers and the effects chain.

### Flash cost

Stored patterns grow from 1 byte/step to 2 bytes/step. A song with 2 machines × 64 steps × 16 voices goes from 2 KB to 4 KB. With 67 Conductor songs, worst case is 268 KB → 536 KB in flash. This needs careful flash layout review — the user_save domain may need to grow.

**Migration:** Old patterns load with condition = 0 (ALWAYS) for all steps. Zero overhead for existing content. The file envelope version field distinguishes old from new format.

---

## 4. Sequencer integration

### Per-step evaluation

In the per-step advance (`bb_audio_section.c` or equivalent):

```c
static bool bb_condition_met(const bb_core *core, uint8_t condition, uint8_t cond_val) {
    switch (condition) {
        case BB_COND_ALWAYS: return true;
        case BB_COND_FILL:   return core->conductor.fill_active;
        case BB_COND_NOFILL: return !core->conductor.fill_active;
        case BB_COND_FIRST:  return core->conductor.loop_count == 0;
        case BB_COND_NFIRST: return core->conductor.loop_count > 0;
        case BB_COND_ALT_A:  return (core->conductor.loop_count & 1) == 0;
        case BB_COND_ALT_B:  return (core->conductor.loop_count & 1) == 1;
        case BB_COND_PRE:    return step_ahead_active(core);
        case BB_COND_NPRE:   return !step_ahead_active(core);
        case BB_COND_PROB:   return (bb_prng(core) % 16) < cond_val;
        default:             return true;
    }
}
```

This runs once per step per voice. At 16 voices × (up to) 64 steps per bar, that's 1,024 evaluations per bar. Each evaluation is a switch + one comparison — negligible at the 44.1 kHz / 256-frame IRQ rate where step advances happen far less often (a step at 120 BPM / 16th notes is ~8 Hz).

### PRNG for probability

Use a simple per-bar PRNG seeded from the bar number:

```c
static uint8_t bb_prng(const bb_core *core) {
    uint16_t seed = core->conductor.bar_number * 7919 + core->current_step * 31;
    seed ^= (seed >> 7);
    return (uint8_t)(seed & 0x0F);
}
```

Same seed → same result within a bar. Restarting the pattern at the same bar gives the same variation. Different bars give different draws. This is deterministic pseudo-randomness, not true randomness — the pattern is repeatable, which matters for recording.

### Fill integration

The quantized fills design arms a fill on pad press and activates at the next beat boundary. With conditional steps, `core->conductor.fill_active` is set TRUE when the fill is active. Steps marked FILL fire; steps marked !FILL go silent. When the fill ends, the flag clears and the pattern returns to its normal form.

This means fills don't need separate fill patterns at all — the fill steps live inside the main pattern under the FILL condition. The Conductor's fill pad simply sets the flag.

### Loop counting

`core->conductor.loop_count` increments each time the pattern loops (wraps from the last step to step 0). It resets to 0 when the Conductor cues a new pattern or song. FIRST fires when loop_count == 0; ALT toggles on the low bit.

---

## 5. UI on the 240×240 display

### Step grid view

Steps with conditions show a small indicator:

```
Normal step:    ●
Conditional:    ●F  ●1  ●A  ●B  ●←  ●%
Inactive cond:  ○F  (step has condition but would not fire this pass)
```

The condition letter sits to the right of the step dot, in a smaller font. On the 240px-wide display with 16 steps visible, each step cell is 15px wide — enough for a dot plus one character.

### Editing a condition

1. Navigate to a step (it's already active — you've placed a hit)
2. Hold the step pad + turn the encoder → cycles through conditions
3. For probability (%), a second encoder turn sets the percentage
4. The condition appears immediately in the step grid

### Live indication

During playback, the step cursor highlights normally. When it reaches a conditional step whose condition is NOT met, the step flashes briefly in a dim colour (showing "this step exists but was skipped this pass"). This gives visual feedback on the variation happening in real time.

---

## 6. Editor protocol

The extended step format affects the editor's snapshot and SET messages.

### Snapshot

The snapshot encoder adds condition and cond_val bytes for each step. The BB1 engine loopback (`packages/engine-bb1/index.mjs`) carries the same format.

### Field schema

New fields in the BB1 adapter:

```javascript
{ id: 0x0050, name: 'step_condition', scope: SCOPE.DRUM_A,
  type: 'enum', values: CONDITION_MAP, timing: 'next_step' },
{ id: 0x0051, name: 'step_cond_val', scope: SCOPE.DRUM_A,
  type: 'uint4', timing: 'next_step' },
// Same for DRUM_B at 0x0052/0x0053 and DILLO at 0x0054/0x0055
```

### Text interchange

In the `.bb1.yaml` format, conditions appear after each step:

```yaml
pattern: |
  BD: x---x---x---x---
  SD: ----xF------xF--
  CH: x-x-x-x%50-x-x-x-x%50
  OH: --------x1------
```

Where `F` = FILL condition, `%50` = 50% probability, `1` = first pass only. Steps without a suffix are ALWAYS.

---

## 7. Priority and dependencies

1. **Prerequisite:** Quantized fills (BB1-QUANTIZED-FILLS-STUTTER-DESIGN.md) must ship first — it introduces the `fill_active` flag that FILL/!FILL conditions read.
2. **Conditional steps** ship after fills, in the same 2.4 release or as a point release.
3. **Loop counting** (`loop_count`) needs adding to the Conductor state — simple uint8 increment on pattern wrap, reset on cue.
4. **Flash layout review** needed to accommodate the doubled step size.

---

## 8. Code cost summary

| Component | RAM | Flash (code) | Notes |
|-----------|-----|-------------|-------|
| Step extension | +4.2 KB | — | Working pattern in RAM |
| Condition evaluator | — | ~200 bytes | Switch + comparisons |
| PRNG | — | ~40 bytes | Per-bar seed |
| Loop counter | +1 byte | ~20 bytes | uint8 in Conductor state |
| UI condition display | — | ~400 bytes | Grid rendering |
| **Total** | **~4.2 KB** | **~660 bytes** | |

---

## 9. Clean-room record

Conditional step behaviour is inspired by Matryoshka 1.17's trig conditions (FILL, first-pass, A/B, PRE, NEI) as described in its public release notes. No Matryoshka source code was read. The implementation uses BB1's own step format, Conductor state, and sequencer architecture. The probability condition and UI design are original additions. The concept of trig conditions has prior art in Elektron instruments (Digitakt, Syntakt) which are well-known in the sequencer community.
