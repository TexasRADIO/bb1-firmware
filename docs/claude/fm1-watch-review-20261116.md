# FM-1 Watch Review — 16 November 2026

**Claude (review) · for Joe**

---

## Feature review — what's worth adopting

### Matryoshka 1.17 — Conditional steps (strongest lead)

**What it does:** Elektron-style trig conditions on individual sequencer steps. Five condition types and their inverses:

- **FILL** / !FILL — step fires only during a fill (or only outside a fill)
- **First pass** / !First — fires only the first time the pattern plays (or only on repeats)
- **A/B pass** — alternates: step fires every other loop
- **PRE** — fires only on the step before an active step (pre-roll)
- **NEI** — fires only if the neighbouring step also fires (chain)

The release also ships 808 and 909 factory pattern/song sets and an SH-101-style patch with arp.

**Why it matters for BB1:** Conditional firing lets one pattern produce multiple variations without storing separate patterns. BB1's Conductor already handles song-level arrangement and performance cues; conditional steps would add *within-pattern* variation — exactly the layer BB1 doesn't have yet. The report calls this "the strongest musical lead" and I agree: it gives the 2-layer drum machine arranged complexity without multiplying the pattern count.

**How it maps to BB1:**

| Matryoshka condition | BB1 equivalent | Notes |
|---|---|---|
| FILL / !FILL | Ties to Conductor fill system | Steps that only sound during a fill pattern |
| First pass | Intro/outro variation | Cymbals crash on beat 1 of the first loop only |
| A/B alternation | Even/odd loop variation | Hi-hat opens every other pass |
| PRE | Pre-roll ornament | Grace note or flam before the main hit |
| NEI | Dependent step | Step fires only if its neighbour also fires |

**Cost estimate:** Each step currently uses bits for voice, accent, and ratchet. Conditions need 4 bits per step (5 types × 2 polarities = 10 values, fits in 4 bits). For a 64-step pattern across 16 voices: 64 × 16 × 4 bits = 512 bytes per machine. Both machines: 1 KB RAM. Within budget on the AC791N.

**UI on 240×240:** When editing a step, the condition is shown as a small tag below the step marker (e.g., "F" for FILL, "1" for first-pass, "AB" for alternate). A dedicated page or encoder-hold gesture cycles through conditions. The step grid's visual brightness could dim conditional steps to distinguish them from always-on steps.

**Action:** Design a BB1 conditional-step system. The quantized fills design from yesterday (BB1-QUANTIZED-FILLS-STUTTER-DESIGN.md) already introduces FILL as a sequencer concept; conditional steps extend that by allowing any step to know whether it's inside a fill. This is 2.4 Conductor work.

---

### AFFINE 0.1 — Jungle performance gestures (bounded comparison)

**What it does:** Maps a break sample into 16 slices with 11 live gestures: roll, reverse, half-speed, vinyl brake, freeze, skip, reverb, crush, and filter. Beta, not yet hardware-tested.

**Relevance to BB1:** The report correctly calls this a "bounded comparison lead." BB1 already has its own performance system (Conductor, effects chain, stutter gestures from yesterday's design). What's worth looking at is the specific gesture vocabulary:

- **Reverse** — play the current slice backwards. BB1 doesn't reverse audio in real time; the IMA ADPCM codec plays forward only. Would require pre-rendered reverse buffers in flash, doubling sample storage. Not practical for 2.4.
- **Half-speed** — halve the playback rate. This is a pitch/speed effect. BB1 could do this by halving the sample read rate, but IMA ADPCM decoding depends on sequential state, so arbitrary rate changes create artifacts. Better implemented as a simple pitch drop on the output mixer.
- **Freeze** — hold the current audio indefinitely. BB1's stutter gesture (designed yesterday) already covers this — it's the 1/1 rate.
- **Roll** — rapid retrigger. BB1's stutter at 1/32 rate covers this.
- **Skip** — jump forward in the break. Not directly applicable to BB1's pattern-based system.
- **Vinyl brake** — decelerate to stop. A fun effect; could be a Conductor gesture that ramps BPM to zero over a user-defined curve.

**What to adopt:** The vinyl brake is the gesture BB1 doesn't have yet. The others are already covered by stutter or effects. Vinyl brake could ship as a Conductor performance gesture in 2.4: press-and-hold decelerates the master clock; release snaps back to tempo (or stops, depending on the gesture mode).

**Action:** Add vinyl brake as a candidate for the 2.4 Conductor performance gestures alongside quantized fills and stutter. Clean-room — behaviour only, no AFFINE code or parameters.

---

### MONO2K 1.9.3 — Install progress display

**What it does:** The FM-1 screen shows a sector grid, percentage and recovery message during installation. The browser installer mirrors the state.

**Relevance:** This is a platform/tooling lead, not a sound feature. BB1's current installer reports progress through the companion app but the FM-1 screen itself goes blank during flash writes. Showing a sector grid and percentage on-device would reduce "is it working?" anxiety. The report notes MONO2K supports both known flash chips (Puya P25Q80 and UC25HQ80).

**Action:** Reference for BB Install Standard. Compare with current BB installer UX. Not a 2.4 priority but worth noting for installer improvements.

---

### FM1Kit 0.8.0 — Resource-aware install APIs

**What it does:** Swift toolkit that can install firmware + sample resources in one operation, write resources separately over USB-MIDI, read resource regions back, and handle CoreMIDI device-change events.

**Relevance:** The resource-write and device-change handling are relevant to the editor's transport layer. FM1Kit's approach to treating firmware + samples as a single installable package aligns with BB1's domain-split transfers (user_save + user_samples in the editor's library protocol).

**Action:** Reference for editor transport hardening. No code adoption.

---

### SY1 0.2 — Searchable libraries with capacity checks

**What it does:** 3,174 Synth1 presets across 27 banks, searchable in-browser. Reads occupied device slots, checks 1,024-slot capacity, confirms overwrites, retains unsent patches. Also adds spoken values and live envelope/LFO/arp feedback.

**Relevance to BB1:** The library management pattern — search, filter, capacity check, overwrite confirmation, retry queue — is exactly what the editor's LibraryItem/DeviceInventory system needs for the BB1 project catalog. The spoken-values feature is a companion/accessibility lead.

**Action:** The report recommends prototyping spoken parameter feedback in the Screen companion first. Agree — this is a companion feature, not an instrument feature. Library UX patterns are reference for the editor's librarian.

---

### Hub card status

- **BB-BOB R35** and **BB–37 1.0.1**: passed automated checks, waiting for Sven's review
- **Compás**: at curator review
- **None live yet** — a green automated check is not a live listing

No action needed — this is waiting on the Hub's curator.

---

## Priority stack for BB1 2.4 Conductor

Building on yesterday's actioned features and today's review:

1. **Quantized fills** — designed, low code cost (yesterday)
2. **Conditional steps** — strongest new lead, extends the fill concept (today) → design doc needed
3. **Live stutter gestures** — designed, moderate cost (yesterday)
4. **Vinyl brake** — new gesture candidate from today's review → add to stutter design
5. Install progress (platform, not 2.4)
6. Spoken values (companion, not 2.4)

---

## Clean-room record

Matryoshka's conditional step behaviour read from release notes and public feature description. No Matryoshka source code was read. AFFINE's gesture vocabulary read from release notes. No AFFINE source code was read. BB1's conditional-step design uses its own sequencer architecture and step storage format.
