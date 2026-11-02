# JIANT workflow and the Dillo kit in private BB1 2.0

This work builds on private source d93d022e73c62e83b354906b80e7b82278e62c04. It does not change public 1.5 or install anything on the FM-1.

## Source and adaptation

Research input: juanjiant-bit/JIANT 62a8432d04e29f2aec7acd1277a6aacca2c0152c, isolated with original notices in the retained JIANT research package. The useful principles are immediate overdubbing, duplicate-cell suppression, count-in/pickup handling, and sound-driven animation. The BB1 modules here use BB1's existing rich phrase, immutable work-part references and arranger journal; they do not copy JIANT's track/save layout, step-wide velocity or silent fourth-note replacement. The new source is GPL-3.0-only. Retain upstream attribution and isolation records when reviewing or reusing original JIANT code.

## What is connected

The private composition-20 recipe enables BB_WITH_DILLO_KIT_VIEW. From GLOBAL on a drum layer, white key 5 opens the Dillo activity view. Normal PLAY, instrument navigation and HOME retain their existing roles. The 16-step editor remains available through normal navigation. The artwork uses fixed integer line geometry: curled kick, head-on snare, sideways toms and opening shells. The view is capped at 20 Hz independently of audio scheduling.

Audio-owner counters increment on accepted rich, legacy-grid and manual-pad triggers. Drum A's serial path counts after successful trigger resolution; drum B's worker path reads resolved hit identities only after its checked join. Failed/pending worker jobs must never animate as committed hits. Counter snapshots use the existing short command guard. Multiple strikes between redraws coalesce into a pulse; this is explicitly not a complete event log. STOP/focus changes clear/baseline motion; a new generation clears old poses while retaining monotonic counters so the first new strike remains visible. Quiet/muted lanes settle within the pulse window. Stopped pad auditions also animate accepted strikes. Alternate percussion sharing a cell shows the active source-voice abbreviation. SY/ZAP have no dedicated character cell in this first layout. Group mute/master zero can make a triggered voice inaudible: the display reports accepted trigger activity, not audible-output measurement. Accepted legacy-grid and manual-pad triggers are now hooked as well, with native alternate-mode and sampled lane-note identity mapping. Unknown sample notes are left unlabelled rather than guessed.

The drawing backend and activity consumer are separate so a reduced-motion implementation can keep activity indicators without animation. The HTML motion study honors prefers-reduced-motion. A physical reduced-motion setting is not yet connected.

## Recording implementation and ownership

bb_live_record implements a bounded overdub pass using the existing 64-step / 128-event rich format. Caller zero-initializes it once. Start frame is an audio-timeline boundary after count-in. Hits before that frame are refused. Nearest-grid quantisation rounds exact halfway hits forward and wraps at the actual phrase length, including eighth-note and triplet grids through the caller's correctly derived step interval. Duplicate voice/cell hits retain all original expression. Protected steps, full patterns, invalid control voices and revision conflicts return specific errors. No heap, flash, source rewrite or implicit history eviction occurs.

The present phrase representation retains accent classes and existing flam/retrigger modifiers. It does not encode arbitrary captured velocity or free microtiming; those require the planned expressive-lane format. Do not label this core as free-time recording.

bb_arr_record_commit creates a new immutable working part with source lineage and one section-edit journal action. Undo/Redo restores the previous/new references. The original catalogue data remains unchanged. A full work directory or journal refuses the entire edit. Shared rows reference the same named section and therefore share its part edit; the UI must offer an explicit section fork if the user wants only one occurrence changed. Caller must obtain stopped main-owner edit permission before commit; this helper is not a live-audio submission API.

## Next connection, before calling the recorder playable

1. Expose a dedicated LIVE RECORD view alongside STEPS, retaining A/B focus and visible PLAYING / COUNT-IN / RECORDING / REVIEW / UNSAVED states. Final REC/chord bindings must not conflict with existing REC gestures/help.
2. Timestamp accepted pad input independently of graphics and derive grid intervals from the actual tempo/lattice. Quantisation time must not be invented from redraws. Provide immediate audition through existing admission paths.
3. Keep the pass private while recording. Finish presents named KEEP / UNDO / DISCARD. KEEP prepares a complete rich owner and queues it at a safe musical boundary; only the actual APPLIED ticket changes the playing indicator. A refused queue keeps the take for retry.
4. Connect the working part to the same grid editor and project/scene persistence, with explicit source lineage, protected parts, saved-versus-RAM status and readback. A RAM take is never described as saved.
5. Verify count-in/metronome, held pad debounce and the existing Undo framework end to end; then matched real-UI/emulator journeys, native deadline/latency measurements and listening. Do not halve audio buffers merely because JIANT uses a shorter period.

## Evidence boundaries

The test suite uses the actual rich phrase parser, actual 539-section catalogue/source owner and actual arranger journal. The activity observer comparison renders the same real-C arrangement with instrumentation off/on and compares PCM exactly. Native fit is a compiler/link resource result, not native timing. Host UI/NOR/register scaffolds and the synthetic motion study are not physical control, audio latency, persistence, listening or recovery proof. No FWSC/package/install is produced by this step.
