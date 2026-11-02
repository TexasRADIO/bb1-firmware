# Named-section model: implementation status

Implemented in `firmware/src/bb/bb_arrangement.c` and `.h` for the main loop.
This is a source-only composition component. It is not yet linked into the
native recipe, audio owner or UI, and is not the alpha1 package previously
installed on the unit. No new hardware run or native RAM-fit claim follows.

The fixed model supports 32 named sections, 64 ordered rows and 16 reversible
edits. Each section references complete A/B/Dillo objects by stable ID,
revision and curated/working ownership. Dillo may be absent. References do
not flatten rich phrase data, reinterpret kit numbers or duplicate source
music. The resolver must still verify the actual source registry and prepare
full indexes/controls and Dillo expression. Parts have independent references;
section duplication/forking is explicit rather than silent shared mutation.

Rows support insertion, deletion, replacement, reordering and 1–64 repeats.
Sections support named creation, revision-qualified editing and unreferenced
removal. Duration is explicitly in sixteenth-note units; it does not impose a
16-step source lattice. No eight-part song assumption is made. New IDs are
never reused, including after Undo. Changes invalidate stale save completion;
a save is acknowledged only for the exact captured revision. Dirty state is
conservative: Undo to identical content remains dirty until saved again.

Undo/Redo records complete section references and names or exact row edits.
Refusal leaves the entire model and history untouched. Capacity is explicit:
section full, row full, history full and exhausted identity/revision counters
are distinct results. History does not silently evict old edits; a subsequent
UI may explicitly offer discard through a revision-qualified history operation.
That operation changes neither music nor its saved/dirty state. The model is
5,456 bytes on the tested host ABI, including 2,624 bytes of history. This is
not a target link-map allocation measurement. No project codec exists yet;
RAM history must never be deserialized from project files.

## Verification

Run `bash big-beat/composition-20/tests/run-arrangement.sh OUTPUT_DIRECTORY`
from an existing output directory. Strict C11 warnings and ASan/UBSan pass
120,152 checks, including:

- INTRO → VERSE×2 → CHORUS → BREAK → CHORUS → ENDING;
- shared-section changes and stale revision/save refusal;
- all three capacity errors and revision/ID exhaustion;
- malformed history kinds/indexes and stale referenced objects;
- full history Undo/Redo, branch-after-Undo and non-reused identity;
- 2,000 deterministic multi-edit trials with complete musical content restored
  after Undo and reproduced after Redo.

## Owner connection still required

Update: the actual atomic rich A/B/Dillo owner is now implemented and qualified
at the scope recorded in SECTION-OWNER.md. The model's source resolver,
automatic arrangement transport and actual UI still need to connect to it.
The following owner contract remains the requirement for that connection.

The current independent rich-phrase and Dillo publishers cannot be called
sequentially and described as an atomic section operation. The integration
must reserve and prepare a whole bundle outside the callback, preflight both
actual engine capabilities and every delayed-queue/generation constraint,
and apply or reject all three parts at one musical boundary. Pattern-only
changes must retain sounding tails and live sound/mix edits. Queue/seen state
must remain distinct from APPLIED, and section history/PLAYING status must
not advance on a refused or stale audio transaction. Model mutations above
are authoring changes only; they do not pretend audio acknowledgement.

No new public assets, source tag or 1.5 package changes accompany this module.
All six 2.0 workstreams and the remaining native/UI/hardware/listening gates
in PLAN.md and ARRANGER-CONTRACT.md remain active.
