# r9 catalogue: approved content packed into rc2

```
Author   Claude (reviewer), r9 lane step 1 (PLAN.md, revised 19:55)
Date     2026-10-27
Base     rc2 = source/rc2-canonical fc9e196, firmware/src/bb/bb_performance_catalogue.inc (173,387 B BBPC v2:
         boot42's catalogue + 138 Roland presets appended in 650afce)
Inputs   main 1855f02 + the HOME refits of 80 G, 88 D and 89 F (2026-10-27 evening, section 4.1)
Output   out/rc2-catalogue.patch (git apply on fc9e196) = out/bb_performance_catalogue.inc, 174,516 B BBPC (+1,129 B)
Scope    host only; no firmware built (the iMac lane owns the tree), no device contact
Labels   DEMONSTRATED (run here) · HYPOTHESIS (estimate) · OPEN (not done)
Status   ALL GATES PASS. G5c: all 21 eras load on rc2's HOME era loader (was 18/21). Three approved era parts were
         refitted, the smallest change each (section 4.1): 80 G conga HC -> LC, 88 D flams dropped (hits kept),
         89 F ride RD -> CY. check_drums.py has a new rule, D11, that mirrors the HOME loader so this cannot recur.
```

## 1. One command

```sh
python3 claude-code/r9/catalogue/pack.py            # gate 1, then build out/ (about 20 s)
python3 claude-code/r9/catalogue/gates.py           # gates 2-6 with rc2's own decoder and loaders (about 15 s)
python3 claude-code/r9/catalogue/audio_account.py   # per-section audio accounting (about 5 min)
```

- `pack.py` reads every source from the working tree and rc2/boot42 from git, so a changed drum or Dillo file is picked up by re-running these three commands.
- `--hold 82-synth-pop` keeps rc2's 82 drums and skips its refit. Result: 174,983 B; BBPC sha `9069cd08…`, `.inc` sha `3d89c54cac1a871248ea3570980145b5e9158dc2764c2950e69a4e572fd5db9f`.
- `--mode append` and `--song-expression positional` are the alternatives described in section 5.

| File | Role |
|---|---|
| `bbpc.py` | BBPC v2 codec: decoder mirroring `bb_paged_container.c` / `bb_paged_records.c` / `bb_catalogue_page.c`, encoder, `.inc` writer |
| `pack.py` | gate 1, then builds the r9 JSON from rc2 + the approved sources, encodes it and writes the patch, reports and `inputs.json` |
| `gates.py` | gates 2–6: C re-dump, check scripts, field diff, firmware read paths (`probe.c`), size |
| `probe.c` | host probe linking rc2's own loaders (drum read, 808 and 909 projections, rich phrase, synth data, recipe, HOME trio) |
| `audio_account.py` | the retained 342-section integration fixture, adapted to 480 sections and to each section's own Dillo line and era preset |
| `out/` | `bb_performance_catalogue.inc`, `rc2-catalogue.patch`, `cat-r9.json.gz` (dump), `pack-report.json`, `gates.json`, `diff-report.json`, `audio-accounting.json`, `inputs.json` |

## 2. Gates

| # | Gate | Result | Evidence |
|---|---|---|---|
| G1 | Encoder reproduces rc2 byte for byte | **PASS** · DEMONSTRATED | rc2's 173,387 B is rebuilt with sha `6ca377ee…` and its `.inc` text is identical. Method: boot42's JSON dump encoded canonically, then rc2's dump appended, then every page LZ4-packed. The same encoder also rebuilds boot39's 128,589 B catalogue byte for byte, and boot42's record layout. |
| G2 | Re-dump equals the source JSON | **PASS** · DEMONSTRATED | rc2's `dump.c` (firmware decoder) output equals `pack.py`'s JSON. 342 Dillo lines, 119 pad/lead parts and 168 drum grids each equal their source (after refits). |
| G3a | `check_dillo.py` | **PASS** | 0 failures, 0 warnings on the 342 approved sections. The whole catalogue gives 5 failures and 138 warnings, the same as rc2: R2/R4 on the 138 Roland presets, which are drum-only with one shared silent Dillo line ("no companion parts"). |
| G3b | `harmony_check.py` (source lines + overrides) | **PASS** | 0 strong pad, lead/bass or pad/lead clashes (+2 allowed, 85 Intro) |
| G3c | `harmony_check.py`, catalogue lines/pads/lead only | **PASS** | same as G3b, with the Dillo tree rebuilt from the dump and no overrides |
| G3d | `check_pads_lead.py` on the dump | **PASS** | 0 strong pad/lead clashes; 0 clashes added by a proposal |
| G3e | `check_drums.py`, all 21 eras | **PASS** | 0 failures, 0 warnings, including the new D11 (HOME loader mirror). On rc2's dump D11 flags the same 16 parts in the same 7 eras as the probe; on the r9 build before the refits it flagged exactly 80 G (LANE), 88 D (MODIFIER) and 89 F (VOICE). |
| G4 | Nothing else changed | **PASS** | 20,287 changed leaves, all in the allowed set: Dillo `bass`, era `pattern.events`, era `dillo.settings`, 89's `swing`. The 138 Roland entries, every song field and the top level are identical to rc2 (`out/diff-report.json`). |
| G5 | Firmware read paths, every section (rc2 loaders) | **PASS** | All 480 sections pass drum read, rich-phrase prepare, effective 808 voice admission, Dillo and Telegram synth data and era recipe. Era parts with 808-grid issues: 16 → 0. |
| G5b | Format limits | **PASS** | 8,623 records < 65,535; 174,516 B ≤ 1 MiB (`BB_PC_MAX_BYTES`) |
| G5c | HOME era load (`bb_factory_prepare_trio`, REC held) | **PASS** | Loadable eras 14/21 in rc2 → **21/21** in r9. Newly loadable: 81, 82, 85, 86, 88, 89, 90. Newly refused: none. 80 loads again. Section 4.1. |
| G6 | Size within budget | **PASS** · HYPOTHESIS until linked | The catalogue grows by +1,129 B. rc2 has 18,348 B code free in the 581,564 B XIP slot (`app.ld` `XIP LENGTH 0x8DFBC`; `build-report.json` on `codex/rc2-release-docs`), so about 17,219 B remain. |
| G7 | Per-section audio accounting | **PASS** · DEMONSTRATED | 480 sections; 359 changed, all 359 predicted, 0 unexplained, 0 predicted-but-unchanged (section 3) |

## 3. Audio accounting

The fixture is `claude-code/timing/tests/integration`: two native 808 kits, Dillo, echo/spring, worker failures and STOP tails. It was built with its retained command line against rc2's tree, once with each catalogue. Three changes were made to a copy:

1. the section count is the catalogue's (480), not 342;
2. each section's Dillo plays that section's own catalogue line and expression;
3. an era section applies its era preset.

Prediction from the probe: section *s* changes if and only if one of these changed:
- the rich phrase of any of its four drum sections (*s*, (7*s*+3)%N, (*s*+1)%N, (*s*+171)%N);
- its own Dillo data;
- its own era recipe.

| Changed sections | Cause |
|---|---|
| 15 eras | own drums, own Dillo (Ending refit) and own era preset |
| 150 eras | own drums and own era preset |
| 3 eras | own era preset only |
| 191 songs | only the paired era drums (the fixture renders 4 sections' drums per section); their own data is identical to rc2 |
| 121 | unchanged |

rc2: energy 136,413.54. r9: energy 152,765.66. Same calls (429,600), submissions (1,920) and retirements (53).

Pads (Greyhound) and lead (Telegram) are inaudible in rc2: Telegram is compiled out (`BB_WITH_TELEGRAM=0`) and no firmware reader takes a part's `greyhound` notes. No pad/lead array differs from rc2 anyway (section 4.6).

## 4. Findings

### 4.1 HOME era load: 80, 88 and 89 refitted, all 21 eras load (G5c, DEMONSTRATED)

- **Path.** HOME (`bb_home_input.inc:39-45`, included at `bb_target_ui.c:943`) loads an era through `bb_performer` → `bb_factory_request_trio` → `bb_factory_prepare_trio`. That function refuses the whole era if any part has an 808-grid projection issue (`bb_factory_trio.c:53-70`).
- **Rules.** `bb_factory_project_drum` (`bb_factory_drum.c:124-147`) puts LT/LC, MT/MC, HT/HC, RS/C and CP/MA on shared lanes, each in one mode per part (LANE). RD, SY and ZAP have no lane (VOICE). Any modifier, such as a flam, is an issue (MODIFIER, line 138), as is a voice twice on one step (DUPLICATE, line 137).
- **Before the refits** three approved parts were refused. Joe approved all of these drums, so each got the smallest change that loads and keeps the music:

| # | Part | Issue | Change | Why this one |
|---|---|---|---|---|
| 1 | 80 New Romantic G (Fill 2) | LANE: conga `HC` (4 7 10 12 14 16) and tom `HT` (26 27 30) on lane 4 | The conga bar plays on `LC`, the LM-1's low conga; same steps and accents | Lane 2 is free in G (no `LT`). `LC` is an LM-1 voice (`machine_voices.py`). C and F keep `HC` (no `HT` there). rc2 loaded this era; r9 broke it, and this restores it |
| 2 | 88 Balearic D (Fill 1) | MODIFIER: flams on `SD` 9 and `HT` 13 (kept from boot-28) | Flams dropped; both hits stay as plain hits | The previews never sounded the flams (render2.py has none), so what Joe approved by ear is unchanged |
| 3 | 89 Detroit F (Main 3) | VOICE: ride `RD` on 3 7 11 15 | The ride moves to `CY` on the same steps | `OHH` already plays 3 7 11 15 in F, so `RD`→`OHH` would be DUPLICATE (still refused) and would lose the lift. `CY` is the voice rc2's player path already plays for `RD` (`bb_voice_fallback.h`, RD→CY first), so HOME now matches the player. 808 grid: `CY` = lane 8, free in F. 909 kit: `CY` = the 909 crash (`bb909_sequence.c`), a 909 voice (D8 passes). Trade-off: on the 909 kit, where `RD` had its own ride lane, the off-beats now sound as crash |

- **Result.** Loadable eras on HOME: rc2 14/21, r9 before the refits 18/21, r9 now **21/21** (probe: trio OK for every era).
- **Permanent gate.** `claude/tests/drum-audit/check_drums.py` rule **D11** mirrors the HOME loader: LANE, VOICE, MODIFIER, DUPLICATE, TIMING and drum velocity/offset. It agrees part for part with `probe.c`: 16 parts in 7 eras on rc2's dump, 3 parts (80 G, 88 D, 89 F) on the r9 build before the refits, 0 now. G3e runs it on every pack.
- **Not affected:** the player path (`bb_player.c:75-89`, rich phrase), where all 168 era parts are admitted, before and after.
- **Audio.** `render2.py` gained `LC` (a low conga, about 190 Hz) so the 80 G change can be heard. Changed WAVs: `80-romantic-drums-G-fill-2.wav`, `80-romantic-drums.wav`, `89-detroit-drums-F-main-3.wav`, `89-detroit-drums.wav`. The 88 WAVs are byte-identical (flams were never rendered).
- **Tool fix.** `audio_account.py` now skips `probe.c`'s per-era `trio` rows, which it read as sections (section count mismatch). The fixture and its prediction are unchanged.

### 4.2 909 kit

On a 909 kit, `bb_factory_request_drum_pattern` uses `bb909_sequence_project` (`bb_factory_drum_launch.c:30`), which has no CB, congas, claves or maracas. 25 era parts use those period voices: 79 G-H, 80 C F G, 83 A B D-H, 85 A-H, 87-chicago F, 88 D (cowbell), 98 G and the bb-study 84 C G. Those parts are refused if Joe plays them on the 909 kit. This is a limit of the 909 voice set, not a catalogue error (`gates.json` → `new_projection_issues` lists those new against rc2). No r9 era part is refused on the 909 kit for any other reason (flams, duplicates or RD): 88 D lost its flams, and 89 F's `CY` is the 909 crash.

### 4.3 Route A (SY→CB, ZAP→HT on drum B)

No era part uses SY or ZAP, so the new drums do not interact with Route A. The songs that use SY or ZAP (for example Egypt) are byte-identical to rc2.

### 4.4 Dillo lengths (DEMONSTRATED)

- **Supported.** 16-, 32- and 64-step lines are accepted for eras and songs. `bb_factory_pattern.c:26,29` takes `count ≤ BB_STEPS` (64, `bb_core.h:12`) with `lengths.dillo == count`, and the trio checks `bb_trio.c:29`. All 342 lines load.
- **Note, not a defect.** Each synth loops at its own length (`bb_core.c:266`) but restarts at step 1 on every part change (`bb_parts.c:75`, `prepared_parts.c:88`, `bb_audio_pattern.c:81`). Part changes land on the drum phrase (`prepared_bank.c:221-223`). So a 32-step line under 16-step drums plays its second bar only while the part is held. This affects eras 88, 89, 92 and 94, and 18 songs. A firmware change would be needed only if Joe wants changes to land on the Dillo line's own boundary.
- **Ranges.** Notes are 0–127 (`bb_factory_pattern.c:44`). Velocity is an integer 1–127 and gate an exact k/100 (`bb_note_expression_catalogue.c:7-11`). All pass.
- **No firmware change needed** for any packed value.

### 4.5 Presets (`settings_suggested`)

- **Eras.** Each era has a matching per-era preset field (`dillo.settings`, read by `bb_factory_recipe.c:39-63`), so the 105 values that differ in 21 eras are applied. These were the settings of the approved previews (`render2.py`).
- **`saw`.** `wave: "saw"` is written as `"sawtooth"`, the only saw spelling the firmware accepts (`bb_factory_recipe.c:56`); `"saw"` would refuse the era (RECIPE_MODEL). Wave changes: 80, 88, 94 square→sawtooth; 87-chicago sawtooth→square; bb-study-82-boogie-funk square→sawtooth.
- **Songs: not applied.** The firmware reads no per-song Dillo preset; `song.settings` is never read. Joe and Codex to decide:

| Song | Suggested change (current → suggested) |
|---|---|
| 777-9311 | wave square→sawtooth, cutoff 780→760, resonance 4→3.2, env 44→40, decay 20→18, accentAmount 68→64 |
| Bassline | cutoff 740→600, resonance 10.5→3.2, env 68→48, decay 19→22, accentAmount 76→66 |
| Behind the Mask | cutoff 1120→700, resonance 5→3, env 48→36, decay 16→20, accentAmount 64→56 |
| Blue Monday | wave square→sawtooth, cutoff 430→520, resonance 2→2.2, env 26→30, decay 28→22 |
| Clear | cutoff 430→560, resonance 2→2.6, env 26→32, decay 28→24, accentAmount 52→55 |
| Computer Love | cutoff 400→420, decay 44→40 |
| Egypt, Egypt | cutoff 960→520, resonance 9→2, env 64→16, decay 23→18, accentAmount 74→55 |
| In the Air Tonight | wave square→sawtooth, cutoff 430→300, resonance 2→1, env 26→10, decay 28→60, accentAmount 52→22 |
| It's Yours | cutoff 430→340, resonance 2→1, env 26→12, decay 28→42, accentAmount 52→26 |
| Numbers | env 32→26, accentAmount 58→55 |
| Paul Revere | cutoff 430→360, resonance 2→1.2, env 26→14, decay 28→38, accentAmount 52→30 |
| Planet Rock | cutoff 840→760, resonance 4.5→3, env 38→36, decay 34→30, accentAmount 48→50 |
| Posse On Broadway | cutoff 430→460, env 26→28, decay 28→26, accentAmount 52→50 |
| Radioactivity | cutoff 350→380, resonance 1.2→2, env 18→20, accentAmount 28→32 |
| Sexual Healing | cutoff 430→380, resonance 2→1.2, env 26→16, decay 28→46, accentAmount 52→30 |
| Tainted Love | cutoff 510→340, resonance 2.5→1, env 28→12, decay 36→30, accentAmount 42→40 |
| The Model | cutoff 710→640, resonance 3→2.6, env 40→34, decay 18→20, accentAmount 64→58 |
| The Robots | cutoff 620→600, resonance 3.5→3, env 36→34, decay 25→24, accentAmount 60→58 |
| Trans-Europe Express | cutoff 560→400, resonance 2.5→2, env 30→16, decay 30→32, accentAmount 55→42 |
| True Faith | wave square→saw, cutoff 680→380, env 38→22, decay 28→24, accentAmount 61→60 |
| Voodoo Ray | wave square→sawtooth, cutoff 430→700, resonance 2→3.6, env 26→60, decay 28→22, accentAmount 52→72 |

### 4.6 What rc2 already had

boot42 (and so rc2) already carries Codex's 11:57 packing (`codex/dillo-catalogue-pipeline`, `pack_bbpc.py`). That packing includes:
- all 174 song lines and 167 of the 168 era lines;
- every telegram pad/lead override and Egypt in E minor;
- the 77 Motorik and three bb-study drum versions.

r9 verifies all of these against the current sources (G2). The new content is:

| Change | Count |
|---|---|
| Era drums rewritten | 146 parts in 20 eras (88 D since its flams were dropped). 77 Motorik's drums are already in rc2; 19 bb-study parts changed since Codex's pack. |
| Dillo Ending refits from `dillo_refit` | 15 eras (77 79 80 81 82 83 85 86 87-chicago 87-hip-hop 88 89 90 96 98) |
| Era presets | 105 values, 21 eras |
| Swing | 89 Detroit 0 → 15 (approved in the drum sheet) |

Two details:
- **77 Motorik H.** It differs from rc2 only in the note values of rest steps. The refit's rests carry the last sounding note (the drum libraries' `apply_refit` rule); Codex's packing left the old rest values. The sounding notes are identical.
- **Flams and ghosts.** `pack.py` writes a flam in the catalogue's own encoding (`modifiers: ["flam"]`, `source_role: "flam_principal"`), but no r9 era part has one: HOME refuses any modifier (section 4.1), and `check_drums.py` D11 fails a flam before it is packed. Ghost notes have no catalogue encoding (no drum velocity, `bb_factory_drum.c:83`), so they are plain unaccented hits, as the drum generators intend (`d3_lib.py:11`).

### 4.7 Song expression (decision for Joe/Codex)

- **Original convention.** In the boot-28 catalogue, a song's Dillo velocity and gate are set per step by part role, and the lead uses the same values on every step. Intro is 78/0.7. Main is 90/0.7. A fill is 90/0.45 with a 102 swell on steps 13–16.
- **What rc2 does.** Codex's derivation gives every accented note 102 and every other note the part's lower velocity. Accents are therefore louder twice: by the 303 accent and by gain (velocity/104, `bb_dillo.c:106`). The fill swell is lost.
- **This pack.** r9 keeps rc2's values (no unapproved sound change).
- **Alternative.** `pack.py --song-expression positional` restores the original convention, taking the per-step values from each part's lead.

### 4.8 Other

- `pattern.era.source.eventCount` is not updated for rewritten drums. Codex did not update it either, and the firmware never reads it.
- 89 Detroit's swing of 15 is odd, so the recipe flags HALF_SWING (`bb_factory_recipe.c:48`). It is accepted, as 86's 7 already is.

## 5. Encoder notes

- **Record rules.** Records are interned bottom-up, deduplicated by value. A canonical object interns its values first, then its key array; an append layer interns the key array first, as `append_catalogue.py` does. Text is in record order and shapes are sorted by record ID.
- **LZ4.** Each 768-byte page is LZ4-packed with a line-for-line Python port of `repack_catalogue.mjs` (`claude/issue-14-pattern-audit`).
- **Why rc2 is rebuilt from two JSON layers.** rc2 was made append-only (boot42 + Roland). It holds 2 unreachable records (the old root and `songs` array), so no single JSON dump can rebuild it. Replaying its history can, and does.
- **Default output: compact.** The r9 output is the canonical encode of the r9 JSON (174,516 B, +1,129 B). It is 368 B smaller than before the HOME refits: 88 D held the last flam events, so the event shape with a `modifiers` key and its flam texts go (shapes 40 → 39, records 8,628 → 8,623).
- **`--mode append`.** This keeps every rc2 record ID, but strands each replaced array (188,202 B, +14,815 B; only about 3.5 KB of code space would be left).
- **Renumbering is safe.** Record IDs are never persisted: `bb_session.h` stores explicit fields, and `bb_factory_choice` lives in RAM.

## 6. Hand-off hashes (PR #45 `HANDOFF.json` format)

### Outputs

| Name | Path | Bytes | SHA-256 |
|---|---|---|---|
| r9 catalogue C include | `claude-code/r9/catalogue/out/bb_performance_catalogue.inc` | 927,364 | `743a34586ca6394c0e17c0db881d3a38b9a6e99e0fa9e9eea9d483d6503ef1f7` |
| r9 catalogue BBPC (inside the include) | — | 174,516 | `b923c3b350e0a316b483bee944362a27ac7977ed6f4ce7a86b43e1760503371e` |
| Data patch on fc9e196 | `claude-code/r9/catalogue/out/rc2-catalogue.patch` | 1,769,835 | `73ecf65011d6a28627559a81a72f1b89305a89e52d2196c703f5912e1d9ffea1` |
| Dump (gzip, `gzip -9n`) | `claude-code/r9/catalogue/out/cat-r9.json.gz` | 124,487 | `8acc7bb1ae9de39fe89976dd6102e5c23d9f31d09c9a4b838735001bcfe161ba` (JSON `f2f371974302db129965620b0e5b37368dd7e6ba260676c6446a250c12200246`) |
| Alternative, `--hold 82-synth-pop` | not committed (one command) | 174,983 | `.inc` `3d89c54cac1a871248ea3570980145b5e9158dc2764c2950e69a4e572fd5db9f` |
| Base rc2 C include | `git:fc9e196:firmware/src/bb/bb_performance_catalogue.inc` | 921,305 | `87455aae91f996ce73ee64b6fe4e233d74fc800ff4a165ab7c6127dd541c42df` |

### Inputs

These entries are also in `out/inputs.json` in the PR #45 `inputs` shape (name, path, sha256, bytes). Row 2 is used only by gate 1.

| # | Path | Bytes | SHA-256 |
|---|---|---|---|
| 1 | `git:fc9e196ddba065084152fb873cb50d7c30177af7:firmware/src/bb/bb_performance_catalogue.inc` | 921305 | `87455aae91f996ce73ee64b6fe4e233d74fc800ff4a165ab7c6127dd541c42df` |
| 2 | `git:ceb0986:firmware/src/bb/bb_performance_catalogue.inc` | 785272 | `8c701285f19ab2f8065b66b05300d4b99495b1253ada9d4935e09219b3bb941a` |
| 3 | `claude/drums/79-minimal-drums.json` | 5017 | `63318e833f31efb38cdbc65b925e0c9ea6d8be07ef674d9925dcef7a4cadb5e0` |
| 4 | `claude/drums/80-romantic-drums.json` | 8746 | `2ad80e54520ee1230daaa6b22d3d3895185898dc25887871a55c874a1ed816ec` |
| 5 | `claude/drums/81-futurist-drums.json` | 4771 | `1a4100e8aa5aa80686f4ce463a247d8250a425f61c4908a76a7b88094f190bc7` |
| 6 | `claude/drums/82-synth-pop-drums.json` | 7315 | `3dafc75260c140ebe28ec851b40dfd7dd430bcd91cb1d3ee2fc072a662904149` |
| 7 | `claude/drums/83-electro-boogie-drums.json` | 5597 | `01fa37563beddbfe8cfa5ad9f441e4e0c4e00bd409a68487c7df255440bf956a` |
| 8 | `claude/drums/84-hi-nrg-drums.json` | 3964 | `1db29419a1c145c6db058b50a048c62fccdbc80117e929e7bb45ac65efa250b5` |
| 9 | `claude/drums/85-industrial-pop-drums.json` | 4298 | `59689f826f1f6e1d03eb507cab33f85cda94eda373762558e48e6d97fb5ded54` |
| 10 | `claude/drums/86-acid-house-drums.json` | 4992 | `8d27c44511081e18dce67c3790bc00a467c80c93860728b83ae4418928983ad1` |
| 11 | `claude/drums/87-chicago-drums.json` | 6841 | `4fdf8028e1f2397c67ebb89c684d2e1f229ae0ffa9a072903dddc422d71440a9` |
| 12 | `claude/drums/87-hip-hop-drums.json` | 5117 | `66f6a68e700c8121b6b70341baeccd1683bf655788cff235723be210671d494d` |
| 13 | `claude/drums/88-balearic-drums.json` | 4198 | `4a1354fc6287797c0d88a7cd069dc1130e016501aa3ff924bc92440cc6ab71a9` |
| 14 | `claude/drums/89-detroit-drums.json` | 5375 | `f2af38d68394d8de2f4f2cc1e270759bab4dab03e3d29f9c8fd267c01be8e3ff` |
| 15 | `claude/drums/90-rave-drums.json` | 6975 | `cc604bb0c9f95467e7cf6b3e0ce40399ae9b16fb34dd4d220ba9e97e30deacb1` |
| 16 | `claude/drums/92-boom-bap-drums.json` | 4811 | `0ede0cf2689c0487d18aa464290a3c49040fd61049192b4e7cfe7ee8b936ba23` |
| 17 | `claude/drums/94-trip-hop-drums.json` | 4521 | `709ec6eb6c952bd8e50224f8545ff592dc942b9d3ec7a0da24799617dd89f0cf` |
| 18 | `claude/drums/96-big-beat-drums.json` | 5586 | `442cd49287ca014de0b73ee315fcf3364446aae5b4a074befd1c924b21f055f3` |
| 19 | `claude/drums/98-garage-drums.json` | 5949 | `67b8b440b5c05a889400404bfca90dbbe9a04eabccdfe3558533765eaf7b96fa` |
| 20 | `claude/drums/bb-study-82-boogie-funk-drums.json` | 8944 | `b79e8bdbec947690c18858d01221d190974762213ee91459766d35cc363213c0` |
| 21 | `claude/drums/bb-study-82-ny-electro-drums.json` | 8617 | `40b7f448dde84a388705bf15e73104ffc1f79b219035486e323d77139ce0cdb3` |
| 22 | `claude/drums/bb-study-84-la-electro-drums.json` | 8438 | `e71b5d39d01016059e7bace10ec789b2ecf397152c0c5ed899122efefabac940` |
| 23 | `claude/drums/pilot-77-motorik-drums.json` | 6284 | `060b61c3aaad7a698be9a383973c6f857cae0ab6e18cbe3218c134c047572d66` |
| 24 | `claude/dillo/pilot-77-motorik.json` | 28078 | `16f18fa062835fd34cec92a0732bdf6161a5421fa8d188e0c4688508d2946b82` |
| 25 | `claude/dillo/eras/79-minimal.json` | 29047 | `47baed4db1b660438e66b6063abce2eeab01869b8a601f0fccf77627da9748ca` |
| 26 | `claude/dillo/eras/80-romantic.json` | 28320 | `524ddcda2a118225ed5012e5508603d664d9e421c6076f88652fa185decc978e` |
| 27 | `claude/dillo/eras/81-futurist.json` | 26848 | `bd24874fa7eb6224716d112f21672d28e1801a0b145061444551682c59534fc9` |
| 28 | `claude/dillo/eras/82-synth-pop.json` | 28989 | `2c11e80364c7ea4932208ebb503b50d0a0f9e796c5230866a9bb1c2cfef5d2ae` |
| 29 | `claude/dillo/eras/83-electro-boogie.json` | 28557 | `c15d4dbd18491bc422e14d13e9d082cffe0b4a0e8f9a97d57a48b71a58a59d0d` |
| 30 | `claude/dillo/eras/84-hi-nrg.json` | 28999 | `2be105dfd4d0bf6b17c68814f203de57dd15280822f2ce20e8128509ae75103d` |
| 31 | `claude/dillo/eras/85-industrial-pop.json` | 28341 | `f5e4e189ddf49a810f6f7c48a043b1f5508e9e9e3a51ee801076f3b2d6ba70a7` |
| 32 | `claude/dillo/eras/86-acid-house.json` | 29002 | `f6738e0d6df79e001928dd7fcb2c8788cca2936257e284dd2fd1253aa0c97d82` |
| 33 | `claude/dillo/eras/87-chicago.json` | 28855 | `e67bb0103ea591ad3cfc3c16d46cab7bc95d8cea0dc0debcc772132f86dbc348` |
| 34 | `claude/dillo/eras/87-hip-hop.json` | 27177 | `5165d9c9ca9dea9af0108ae04131ede7851631aff07c65768219dc3f7d5be54c` |
| 35 | `claude/dillo/eras/88-balearic.json` | 28948 | `13c96258432f2b994c96461b5c9252b89246981fe3aff1c536ad5169e75814f5` |
| 36 | `claude/dillo/eras/89-detroit.json` | 29500 | `98d1a94adc357e3bf8088c637387dd26d1e85a03eeda080f27b158934dc2a587` |
| 37 | `claude/dillo/eras/90-rave.json` | 29095 | `4c5309dea4712df92a4931cc0508dafbcc2b256ad13e65d5ac077b467742e57a` |
| 38 | `claude/dillo/eras/92-boom-bap.json` | 27699 | `7119053a5ced4f24fbf63e6cb38cb490ed37c783a47ec45536270d4cc5e5aa84` |
| 39 | `claude/dillo/eras/94-trip-hop.json` | 27015 | `f8058d7651ee55f202f1a52818f73004388b0a5f2f80d880be6771d49b527847` |
| 40 | `claude/dillo/eras/96-big-beat.json` | 29033 | `235c5cc9e77a88229f733bd1c898dabf675e769f2946b7d3f298e19fed0a9bc7` |
| 41 | `claude/dillo/eras/98-garage.json` | 28653 | `42e846e95a64ee181ffe28b03372698d8e0739ba01610f6ec4ef4037158510e3` |
| 42 | `claude/dillo/eras/bb-study-82-boogie-funk.json` | 52812 | `384a6b2c5929a5d51e0f0c0295c3414721b3704daa11f7271cc1550934e50849` |
| 43 | `claude/dillo/eras/bb-study-82-ny-electro.json` | 52604 | `173477c4385e5cc0122fc1a5390bf4dcd5b0d6cf186013fdbbef77873703603b` |
| 44 | `claude/dillo/eras/bb-study-84-la-electro.json` | 52533 | `9bdf85874049f18459053575712b62e9c257b9f20c21ff6cf8b31d6ed8245d3b` |
| 45 | `claude/dillo/songs/777-9311.json` | 15285 | `22541e38b2caf729b0d89c2332827bd58bd5f6bee49b445beb118bfd789d681b` |
| 46 | `claude/dillo/songs/bassline.json` | 35529 | `23137c074460ff090a914e0291e39e1eb44d640580aedc7f3da58b7bda676953` |
| 47 | `claude/dillo/songs/behind-the-mask.json` | 42505 | `bb16477f9401d3e0802f196693ee7f8f6fc4ecbb18aee485a096daf1b7548cc1` |
| 48 | `claude/dillo/songs/blue-monday.json` | 22195 | `29da091ed9757729cc4d9e3782cecc5a61cadcf97028e4de59a078062a5a4cd3` |
| 49 | `claude/dillo/songs/clear.json` | 27614 | `7f75a1a57a3842c64b43ca09124b8fec42c62ef50578b38a8da32dd2e297fb00` |
| 50 | `claude/dillo/songs/computer-love.json` | 17985 | `58b3f2c1eee113362a938cd074284bab24a47f8c623fc847851a75f1305f8d0d` |
| 51 | `claude/dillo/songs/egypt-egypt.json` | 107648 | `c8fa09289cdd7534b090b5289283cf7ecace2018aa4d40c53085be72263ac243` |
| 52 | `claude/dillo/songs/in-the-air-tonight.json` | 8337 | `f1ce6deb5e4597efab6d81de4cc3bdd8425bc98b0826c1e7cc40126d74242abb` |
| 53 | `claude/dillo/songs/its-yours.json` | 22876 | `b702e16a61203df55c516bb7f657ba5a62d0bc118b9d0728916ab7c0e3ef5c28` |
| 54 | `claude/dillo/songs/numbers-kraftwerk.json` | 47810 | `613d451de3e1eff52ff22df4a6745cf6b464bef7b4a0ca1328ce0a9c4ac89dc9` |
| 55 | `claude/dillo/songs/paul-revere.json` | 29776 | `0a19f75c920c6995fba67ef58ec466092714f3bd7d301fab86a388b405519efa` |
| 56 | `claude/dillo/songs/planet-rock.json` | 8877 | `4032e6a9a659be23e789375dfab59c8f10e5a29f6949e71d149cc8decd122fd8` |
| 57 | `claude/dillo/songs/posse-on-broadway.json` | 43157 | `30c08afec67866d8bce2e3f48331cab93bc54723d0851dbe94660f3e47fbd749` |
| 58 | `claude/dillo/songs/radioactivity.json` | 37653 | `60cd1c5da0bb2104beb7402c4e92fbeafc0a78d403e6d5a5a0671082304d831d` |
| 59 | `claude/dillo/songs/sexual-healing.json` | 8783 | `f48d7c4301068a5a7ddb1d368f8b2fa72c74d223edef47109f0c927ee7cbfe0e` |
| 60 | `claude/dillo/songs/tainted-love.json` | 30706 | `15ce12c44aff0b23d182edbb18bae3edec7f4ea421384bb942622843daa8048f` |
| 61 | `claude/dillo/songs/the-model.json` | 20709 | `b3d7e25a65aa23d1629b2254e0818dd442fdd4d5b431126d80cc722ae1097adc` |
| 62 | `claude/dillo/songs/the-robots.json` | 7689 | `68c448dec51632627ec09816d4c2f67512bcaeab10aeb587585c76fc67728eca` |
| 63 | `claude/dillo/songs/trans-europe-express.json` | 24401 | `85f44cb1d9a1c4c0e7adfa688a2936e9d54a86c9735c857322ef86800f405b33` |
| 64 | `claude/dillo/songs/true-faith.json` | 39193 | `9785cb095a8298ff2575ce04870cccd71702991a1b92bc2498c8fc7680911d76` |
| 65 | `claude/dillo/songs/voodoo-ray.json` | 8802 | `a0ab164faf885cbdffee7b718baacf02109cc44d8566c9724e5f00d74a72f6de` |
| 66 | `claude/telegram/77-motorik.json` | 48938 | `499eeecc36b6fe7d63eac04a67cc51a4321d969742fe1d4abd4c41998aadb265` |
| 67 | `claude/telegram/79-minimal.json` | 23216 | `450524f105b87c39c10273820064dad0223f9b52dd9b47365cd24fea90efe0a5` |
| 68 | `claude/telegram/80-romantic.json` | 45186 | `2699b3b3d103070c69ef5bc21cbb5cb15235b7c04eb8298fc74d7812ac751354` |
| 69 | `claude/telegram/85-industrial-pop.json` | 33333 | `c4aa39fdd4b971e042a0550d8dbf18b488de20cb7b925e7feec3f08807834de0` |
| 70 | `claude/telegram/88-balearic.json` | 23072 | `7d10fae4bccea25d0e10fe6026898554c673ade9a49c1901b939a37edcf60d48` |
| 71 | `claude/telegram/90-rave.json` | 45462 | `713ed454435c7fccbd1243e1102e7a71f3cbd16c07863f54b8c50e00a7926af3` |
| 72 | `claude/telegram/96-big-beat.json` | 46622 | `48fdc9b3070bc668f41f747d81054f45e8f4aa4d15c88cc0a6feb3afedc915c1` |
| 73 | `claude/telegram/98-garage.json` | 44114 | `46fd9e0e61341e80b51f635f8cae938b3d34915c4656f4493078d4d45488b731` |
| 74 | `claude/telegram/bb-study-82-boogie-funk.json` | 48150 | `60c131d0b2763e5411034287318caadc029a720cdcf640ed9d9b1e7e1a49b3f5` |
| 75 | `claude/telegram/behind-the-mask.json` | 45345 | `c5ce4b8b6365636ccff9f7ea168e3f97faa69d72e0d0a0161cf500b2d6d54d95` |
| 76 | `claude/telegram/computer-love.json` | 18646 | `cc5031f8e9ba2d9e8d51c2ea873f7739bd62db086728903a462e21c8f8329ace` |
| 77 | `claude/telegram/paul-revere.json` | 32565 | `efa67a9001d5726894ff69e5fa2a2162857fe7ad9c3aeb50295750f86eb42bd4` |
| 78 | `claude/telegram/planet-rock.json` | 7590 | `108ac2bfe0fac247f1b45d9120ab7b562f451e36775832656f25d766ca19cdf1` |
