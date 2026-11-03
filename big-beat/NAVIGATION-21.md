# BB1 2.1 top-level instrument navigation

Owner direction, 3 November 2026. This supersedes the RC2 interpretation in
which `SELECT` merely moved focus between Kit A, Kit B and Dillo while the
same PERFORM section grid stayed on screen.

## Product model

`SELECT` moves between three top-level musical destinations:

1. **RHYTHM CONDUCTOR** — the coordinated performance surface. It loads a
   song into all three players by default, cues named sections and
   combinations on the bar, and shows what is playing and queued for every
   player. Per-player substitutions remain possible inside the Conductor;
   they are not top-level modes.
2. **BIG BEAT** — one drum-machine instrument. Kit A is its first layer and
   Kit B is an optional second layer, not a second top-level instrument. Its
   landing screen must look and behave like a drum machine.
3. **DILLO** — the acid-bass instrument. Its landing screen is the existing
   playable Dillo keys/filter view, not the Conductor's section list.

The three destinations are peer instruments. Turning `SELECT` must change
both the control target and the main display immediately. A title or outline
change on an otherwise static section list is not sufficient.

## Navigation contract

- `HOME` returns to the landing screen of the currently selected destination.
- On a destination landing, turning `SELECT` exits safely and enters the new
  destination's landing immediately. Owed note releases are completed before
  the target changes. On a deeper editor page, `SELECT` retains its existing
  local A/B/Dillo focus job; tap `HOME` first to change destination.
- `REC+SELECT` remains tempo; a tempo gesture never changes destination.
- Conductor song selection always loads Kit A, Kit B and Dillo. Independent
  substitutions happen afterwards and are shown honestly.
- On a top-level landing screen, `SELECT` changes destination. On a deeper
  player/editor page it retains the existing local A/B/Dillo focus job.
- In the Conductor, black keys B1-B3 focus Kit A, Kit B or Dillo while keeping
  their performance cut/solo behavior. `REC` plus a section key can therefore
  send only the focused player to another section while the others continue;
  the ordinary section gesture still coordinates the group.
- Big Beat keeps distinct A/B audio ownership, patterns and kits while
  presenting them as layers of one drum-machine instrument.
- Dillo keeps its own patch, pattern, live-key and step editing routes.

## Display contract

- Conductor: coordinated section/composition view, including every moving or
  held player and an unambiguous queued state.
- Big Beat: drum pads/machine identity first; show Kit A and the optional Kit
  B layer without implying that either is a separate product mode.
- Dillo: patch name, playable keyboard and filter shape first.
- Contextual help and the guide use the same three names and the same order.
- Rhythm Conductor's approved mark is the third concept from the 3 November
  board: a mirrored R and a C sharing one digital spine, with orange structural
  terminals and one pink accent. The firmware asset must be redrawn on a fixed
  pixel grid (no gradients), remain legible at 48–80 px and never replace the
  written `RHYTHM CONDUCTOR` name where a novice needs orientation.

## Verification

Host tests must cover SELECT in both directions from every page, HOME from
every page, held-note release on a destination change, stopped and running
transport, Conductor local focus, Big Beat A/B independence and Dillo live
keys. Screen captures must prove that all three landing screens are visibly
different. Native build/fit, physical controls, lighting, sound and timing
remain separate gates.

