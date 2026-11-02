Implementation authority,30October2026: Joe instructed Duke to design, implement and assemble2.0 here while Claude is offline, then present the complete work for review. References below to waiting for lead design/adoption are superseded for this development branch; acceptance evidence and review/publication boundaries still apply.

# BB1 2.0 — composition plan v2

Joe's six-priority brief,30October2026. Supersedes product ordering in the initial v1 plan and research priorities file. Builds on the detailed58-profile/53-comparison/12-proposal percussion audit and today's qualified reviews. Research authority and shipped implementation are distinct. Current release remains unchanged.

**Product centre:** two independent drum machines, curated rhythms and original studies, Dillo, shared echo/spring, immediate kit→pattern→play, visible control assignments and responsive Armadillo. Composition grows underneath that instrument. It does not become a generic multi-track workstation with incidental drums.

## 1. A named-section song arranger

Sections have user-visible names and stable working identities, references to A/B/Dillo parts and explicit sound/mix transition policy. Arrange INTRO→VERSE×2→CHORUS→BREAK→CHORUS→ENDING; insert, remove, duplicate, reorder and repeat. Support play-once/loop and a temporary live cue that does not rewrite the saved arrangement. Browsing is read-only; show selected/Playing/Next/dirty state.

A shared-part edit shows its affected sections; Make independent copy forks deliberately. Preserve late user mix edits unless the selected recall policy explicitly replaces them. A queued launch resolves a known revision; stale edits/Undo/scene recall cannot silently redirect it. Keep sounding tails on pattern-only changes; a kit/sound/scene change has its own contract.

**Start from:** existing part/trio/performer and owner tickets; scene-aware arrangement contract; responsive editor prototype; bounded C arrangement document model. That model currently has references/repeats and one Undo record only; named section bundles, rendering, persistence and the actual owner adapter are unfinished. Its32-row host ceiling is not an adopted product limit.

**Acceptance:** create the example song from empty or curated material, edit a shared/forked part predictably, queue a one-shot fill, Undo after a focus change, save/reload, and render/export one finite pass with the same section order/boundaries. Preserve the actual section count of each starter song.

## 2. Per-hit locks, microtiming, conditions, flams and independent drum lanes

Each sound lane belongs to A or B. Author velocity/ghosts/accent, bounded early/late offset, chance/1-in-N/first/FILL conditions, flam spacing and ratchets×1–4. Add sparse tune/decay/tone/send locks only for qualified parameters. Protected-step locks are labelled differently from sound-parameter locks. Define per-lane length/rational rate, straight/triplet grids and intentional mixed subdivisions; do not fake higher resolution by displaying doubled song BPM.

Preserve note/hit ordinals, source articulation, all supported expression, grid and lock metadata through every transform. Define coincident-hit merging, ratchet/flam priority, choke and delayed-event capacity. A missing or unsupported feature produces a specific error rather than truncation. Retain ×1/zero-offset/zero-feel and existing catalogue repeats/flams unchanged.

**Acceptance:** quiet off-grid snare, seven-step shaker against straight kick, triplet hats, per-hit decay lock and phrase-ending roll; copy to another bar without losing expression; save/reload/import/export retains every field. Stress densest admitted combination under the real delayed/CPU/voice budgets. No unmeasured scheduler expansion.

## 3. Seeded, reversible Evolve with protected parts and phrase-aware fills

Extend existing seed/home/freeze/protected masks, not a parallel generator. Prepare editable rich working copies so the feature works with curated source patterns rather than bypassing their owner or losing metadata. Retain immutable curated originals and explicit source provenance.

Original↔draft comparison, Apply/Cancel/Undo and Save as new part share the same acknowledged transaction path. Controls choose seed/amount/density/role and protected parts/lanes/anchors. Use retained Euclid/Random plus deliberate additions/removals, displacement, answer and pickup/fill rules. Start with deterministic rules and authored material; neural/graph generation is not required. Dillo can remain locked while drums vary, or vice versa. No obligatory bass in a drums-only song.

A phrase planner schedules allowed pickups, responses and fills to finish on the intended boundary, respecting downbeats and available voice roles. Define PRNG and condition context across section visits, loops, FILL input, STOP/Start/Continue and export. Freeze a finite realized performance when necessary; never save an accidental unrecorded random state as if it were the intended composition.

**Acceptance:** same seed+declared context yields identical events; protected material never changes; repeatable thinning behavior; fill resolves before the next section without ruining the downbeat; Undo restores complete original musical data. Generation stays outside the audio interrupt.

## 4. Retrospective capture, copy/paste and Undo

Bounded rolling event history remembers input or realized performance under an explicit mode. KEEP LAST1/2/4/8BARS are proposed choices until target memory/stack cost is measured. Preserve downbeat, metre/rate, source/instrument and supported controller/expression data. Declare dropped/coalesced events and never label incomplete history a complete pass.

Explicit overdub/replace, count-in/metronome, range/lane/part/section clipboard and named overwrite footprint. Reference paste and independent copy are different. Shared Undo/Redo operates on the actual committed target and APPLIED ticket, preserves history on refusal, and rejects stale scene/revision. Opening capture does not start transport, change gain or write flash. RAM Undo and restoring overwritten stored projects are separate functions.

**Acceptance:** retain a late-selected good pass at the correct downbeat, overdub without erasing prior work, paste a rich bar without dropping microtiming/locks, Undo across target focus changes, reject stale/full transactions without mutation, disclose ring overflow, and remain stopped when opened stopped.

## 5. Complete project backup, MIDI export and reliable clock synchronization

A portable project includes patterns/sections/arrangement, tempo/metre/grid/groove, seeds and realized condition context, automation, kit recipes, cleared user audio or exact asset references, source/licence references and schema/version/checksums. Unknown mandatory features and missing assets refuse atomically with names/reasons. Test migration, corrupt and interrupted imports, asset identity and unrelated-data preservation. File reopen/readback is required at the claimed persistence level.

MIDI Type1 exports a finite arrangement performance with conductor tempo/metre, separate A/B/Dillo tracks and explicit drum/channel maps. Bake declared random/conditional performance; preserve representable offsets, disclose limitations and retain the lossless project. Verify in two independent MIDI tools. Companion/shared-engine audio render follows; live USB capture and stems are separate optional outputs with explicit tail/nonlinear-return policies.

Start with qualified later-MIDI Continue, default-OFF USB clock output, bend/sustain and scoped program/bank review. Add true phase-aware source/master behavior, loss/reconnect/source-switch policy, source-aware THRU and controller pickup only after exact design/measurement. Do not call integer BPM-follow phase synchronization. Reserve release/STOP capacity, prevent self-echo, count congestion and suspend traffic safely during updates. Tempo display correction belongs in this foundation.

**Acceptance:** complete backup→clean supported restore yields the same music/assets; MIDI re-import/readers agree on finite song timing and notes; Start resets/Continue resumes; measured external receiver phase/jitter and dense USB/MIDI load meet agreed thresholds with no lost release or accidental start. Physical power-cycle/recovery remains an explicit hardware gate.

## 6. Hybrid kits and selected new percussion engines within measured limits

A reference kit combines existing qualified voices per lane with intended roles, levels/tune/decay and choke/articulation policy. Stable asset/recipe identity prevents global edits silently rewriting another project. Preserve current sounds and understandable substitution labels. Use owner-approved repacked assets and listening/permission records; complete scene migration and installer/recovery integration.

Then select one small FM/metal or physical/percussion model for a representative enabled-profile benchmark and matched listening. Measure idle and active costs, code/RAM/pool/cache/stack, CPU0/CPU1/tails/stealing, dense ratchets, Dillo/FX, MIDI and USB together. Preserve fixed release quality and deadline. Fold/LFO/RAM-placement experiments are separate measured inputs, not automatically adopted optimisations; boot/recovery and full audio parity still matter.

Native break lane/sample upload/chop/warp, extra synth engines, long recordings, advanced generators and additional expensive FX remain optional research behind the core composition acceptance. A rhythm-atlas educational view is later presentation work; provenance/role metadata is required now.

**Acceptance:** hybrid recipes roundtrip exactly; missing assets named; predictable hats/chokes/kick priority; no asset corruption on interruption; real combined timing fits and matched listening is approved. Fit-only or competitors' voice counts do not prove BB1 capacity.

## Delivery order and cross-cutting foundations

The six numbered workstreams above retain Joe's priority. Work on stable section/part/revision identity, frame/beat representation, bounded owner transactions, backup/migration and profiling begins immediately alongside workstream1 because later tools depend on them. No feature enters public1.5 through this plan.

First integration milestone: settle lead-approved schema/owner/budget decisions; connect named section/arrangement edits to the real C engine and existing emulator, with complete musical working copies and Undo; expose an honest preview. Second: lane expression and curated-pattern-safe Evolve/fills; capture/clipboard. Third: full restore/export and measured sync. Fourth: compatible hybrid palette and optional new voices only within proven headroom. Sync/restore work is developed throughout rather than postponed to the end.

For each milestone record adopted source, enabled flags, host/real-engine tests, native resource/stack/timing and hardware evidence separately. Keep known current-release controls/persistence/recovery gaps open until actually performed. Preserve ordinary waiting for lead decisions; prepare safe host work rather than duplicating his firmware branches.

## Review provenance

Research pack: /Volumes/Orange Base/Codex Workspaces/big-beat-studio/research/fm1-news-watch/percussion-audit-2026-10-30 . ZIP SHA c3a72c450dbe5b542e3127ddfb2d77b672ca83c8ed7d8741e7b8f1ad731db708. Independent intake checks and proposal mapping accompany this plan. Today's previous review pack remains historical input, including the corrected233song/138preset/168era/205acid counts.
