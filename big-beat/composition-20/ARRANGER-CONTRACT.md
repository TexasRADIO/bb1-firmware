# 2.0 named arranger: implementation contract

Duke owns implementation under Joe's30October instruction; Claude reviews the assembled work afterward. These are the next implementation requirements, not features supplied by alpha1's foundation build.

## Musical objects and ownership

A project owns named sections, an ordered arrangement and editable working parts. A section identifies A, B and optional Dillo parts by stable identity and revision, with an explicit musical duration/repeat and sound/mix-recall policy. A row refers to a section; the number of sections follows the song. Do not force eight sections, split source bars or substitute a solo-instrument part for a complete accompaniment. Existing curated source objects remain immutable with their original provenance.

Shared edits visibly affect their references; Make independent copy deliberately forks a part. Copied or generated data retains every supported ordinal, grid, articulation, expression, protected-step and provenance field. Curated rich phrases must stay rich. The legacy8part/trio API alone cannot represent all current catalogue music and is not an acceptable replacement for this project model.

## Actual audio-owner integration

Prepare complete A/B rich indexes and Dillo data away from audio callbacks. The source resolver works against the actual catalogue/working-copy registry, not a duplicate JS event model. A fixed inactive preparation slot receives a complete section bundle, validated against the actual renderer's voice/grid/event/delayed-queue capacities before publication. Program, section and source revisions bind that slot.

The audio owner needs one atomic section transaction: either both drum parts and Dillo accept the same qualified bundle at the declared boundary or none changes. Sequentially calling the current independent drum publishers does not establish atomic section application. Reuse the current phrase/sequence validators, bounded command queue, seamless handoff and acknowledgement model; implement and test the bundle boundary rather than asserting it exists. No allocation, catalogue decoding, filesystem work or unbounded scanning in the callback.

Pattern-only changes preserve sounding tails, sound settings and late live mix edits. Explicit sound/kit/project recall has a separate policy and validation path. Main-loop status shows the selected row independently from PLAYING, NEXT and UNSAVED; only actual owner acknowledgement advances committed history/playing identity. A stopped selection must not start transport or change master level. Scene/project epoch changes, cancellation, stale revision, exhausted tickets and rejected/full queues cannot leave a misleading PLAYING label or consume Undo history.

The present fixed native RAM-text boundary, audio deadline, sound quality and cross-core join bounds stay unchanged. Measure simultaneous two-rich-part/Dillo boundary cost before accepting the implementation; native fit and a helper test are insufficient. Do not silently lower quality or shorten tails to fit.

## Arrangement transport and edits

Insert, duplicate, remove, reorder, rename, repeat, one-shot/loop and temporary next-section cue. A live cue does not rewrite the saved arrangement. Preserve source song section structure. An arrangement edit identifies exactly which section/references it affects; destructive replace/discard feedback names the target and distinguishes RAM edits from saved data.

Start resets, Continue resumes and STOP cancels pending delayed events/queued changes under the shared transport policy. Musical time derives from the audio frame clock with bounded rational-grid conversion, not integer-BPM display updates or an unbounded least-common-multiple timeline. Section entry, loops, FILL input and repeated visits form part of deterministic condition/random context.

Undo/Redo and clipboard act on complete musical objects. Stage outside the owner, acknowledge application, then commit history; refusal leaves music and history intact. RAM Undo is distinct from restoring overwritten flash/project files. Retrospective input/realized-event capture has bounded storage, explicit overflow and musical timestamps; it does not imply continuous PCM recording.

## Persistence, export and acceptance

A versioned project codec includes all music, arrangement, time/groove/condition state, kit/asset identities and source/licence records. Import/restore validates all required capabilities/dependencies before replacement. Unknown mandatory fields, missing assets and capacity errors identify the actual problem without truncation. Existing1.5 scenes need explicit migration; legacy kit numbers cannot be reinterpreted.

Acceptance: build INTRO→VERSE×2→CHORUS→BREAK→CHORUS→ENDING using real current rich parts and Dillo; edit a shared part, fork it, queue a one-shot fill, Undo after a focus change, save/reopen/reload, and produce one finite MIDI performance with the same section order/boundaries. Verify metadata and audio against actual C owner execution, corrupt/stale/full refusal, native memory/stack and combined dense timing. Current physical controls, power-cycle, recovery and listening gates remain separate until executed. Expand the same implementation for expressive lanes, reversible Evolve/fills, capture/export/sync and measured hybrid kits; do not create competing authoring engines.
