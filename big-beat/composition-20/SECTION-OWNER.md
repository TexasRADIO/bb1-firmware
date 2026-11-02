# Atomic rich A/B/Dillo section owner

This development component connects complete rich A/B phrases and Dillo
notes/expression to the real C audio owner. It is included in the 2.0 recipe
and actual-engine test fixture; it is not published or device-qualified.
The named-section model remains a separate authoring component awaiting its
source resolver, arranger transport and UI connection.

`bb_audio_section_submit` reserves both existing rich inbox slots and one
existing synth inbox slot. It does not allocate another pair of full phrase
indexes in the audio owner. Input metadata is copied outside IRQ masking.
Only this single main-loop producer writes these reservations. A section
command consumes one ticket; all structural/capability/epoch/generation and
queue checks precede every musical mutation. Both rich patterns and Dillo
apply on one owner callback, with identical applied frame/ticket acknowledgements.

Pattern-only application retains drum DSP voices, Dillo sound, live keys,
levels, group ramps, sample bindings and echo/spring state. Old unsounded
rich delayed events are canceled at section entry; sounding tails survive.
Absent Dillo resolves to an explicit rest pattern, not a mute or sound reset.
No kit or sound-recall policy is implied. Existing selected sample kits must
support both prepared phrases or the entire section is refused.

Scheduling uses a bounded 0–4095 sixteenth delay relative to the guarded
submission tick snapshot. Zero means the next actual tick while running;
a stopped selection applies immediately without starting playback. Unsigned
wrap is covered. A preparation that misses its exact tick is refused rather
than silently launching late. The automatic row/repeat/cue scheduler still
needs to feed this API from project duration. No large LCM clock is introduced.

STOP supersedes section commands in the same queue drain and cancels accepted
pending sections. Continue preserves transport position. Stale cancellation
and raw independent-pattern tokens cannot consume another reservation. A
STOP/fault during producer copying marks the preparation canceled without
releasing storage while it is still WRITING. Exhausted tickets/cancellation serials refuse
new work. Full slots or queue refuse submission without musical mutation.
Requested and applied section identities are separate. Accepted independent
pattern edits invalidate the complete-section playing identity; rejected
independent-pattern tokens do not. A UI must consume matching APPLIED state,
not infer playback from selected or queued data.

The initial running test caught an omitted call-level CPU1 exclusion:
P1b planned B through a section boundary and its input guard rejected the
changed B player. The new boundary is now part of the existing
`bb_audio_b_launch_due` predicate. Only that ownership-changing boundary uses
the existing per-span worker path; quality, CPU1 join bound and audio deadline
are unchanged. The failing host trace is retained outside the source tree.

## Verification and remaining gates

`tests/build-section-owner.py OUTPUT_DIRECTORY` compiles the actual catalogue,
control queue, audio renderer, rich metadata, worker stand-in, Dillo and FX
under ASan/UBSan. `test-section-owner` currently verifies all539 stopped and
all539 running A/B/Dillo transitions, malformed partial input, slot/queue
exhaustion, stale epoch/generation, raw tokens, STOP priority, exact cancellation,
producer-copy interruption, late preparation and tick wrap. A 64-block stereo
comparison proves nonzero stopped tails remain bit-identical after replacement;
intervening live group/FX edits survive. Fault draining uses the actual
`bb_audio_fault_drain` path. This is host execution, not device IRQ/DMA timing.

Qualification now passes:523381 section-owner checks,58105 MIDI-owner checks,
466 whole-firmware UI/journal assertions, and the539section/482470call default
PCM oracle exactly matching1.5. The complete rooted77-object native build is
556736B/SHA5a77e1cfb7dd566cd9f8486aa2b9fca3c6b165a5bb9a9693c9cb68d91754cf50,
with24828B code/31364B ordinaryRAM/77140B pool free and24028B RAMtext. All compiled
unit hashes match; link log is empty. Relative to alpha1 this adds2592B code and
64B aligned pool, with ordinaryRAM/RAMtext unchanged. Native entry frames are
96B producer,36B command,4B tick,24B status and20B cancel-submit; this excludes
callees/indirect/IRQ paths. The old name-only callgraph collides on local apply
labels and its broad totals are excluded.

Actual target boundary timing/whole-path stack, production UI/scene/project
integration, full WASM, listening and recovery acceptance remain separate gates. Historical alpha1
hashes/hardware results in HARDWARE-2.0-ALPHA1.md identify the earlier foundation;
they do not qualify this new section-owner component.

Native qualification explicitly roots the producer, status and cancellation
APIs. An initial consumer-only link is retained as historical output, not the
complete component cost: otherwise unused producer functions would be garbage
collected before the UI caller exists. This is code retention for measurement,
not a claimed UI connection. The 5,456-byte project model is not yet allocated
in the native main loop; its eventual controller memory must be measured too.
