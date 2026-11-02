# Private 2.0 portable project v1

Draft private integration. The codec stays cold; the optional stopped project owner supplies the preview capture/restore bridge. This is not a native file driver, flash format or public release. The existing 1.5 scene journal remains unchanged. Unknown required format features refuse; no silent downconversion.

## Framing and identity

Maximum32768 bytes. All integers little-endian, no native padding, runtime pointers or cached catalogue positions.64-byte header:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | BBP2 |
| 4 | 2 | Version1 |
| 6 | 2 | Header64 |
| 8 | 4 | Exact total bytes; no trailing data |
| 12 | 4 | IEEE CRC32 over bytes0..11 and16..end |
| 16 | 4 | Required features1; unknown bits reject |
| 20 | 4 | Exact catalogue container CRC |
| 24 | 4 | Exact catalogue container stamp |
| 28 | 4 | Resident sound-library header CRC |
| 32 | 4 | Resident sound-library payload CRC |
| 36 | 4 | Nested scene byte length,1..3840 |
| 40 | 1 each | Active-section, row, curated-reference, working-part counts |
| 44 | 4 | Arrangement revision |
| 48 | 4 | Next never-reused section ID |
| 52 | 4 | Next immutable working-part revision |
| 56 | 8 | Reserved zero; nonzero rejects |

These are compatibility/integrity pins from the existing engine containers, not cryptographic authentication or a new sample licence. Independent release/library SHA256 custody remains external. Missing/different libraries or catalogues refuse; no automatic migration or substitution into another source.

## Payload

1. Active sections in model order: ID32, revision32, name24 including NUL, three references (ID32/revision32/source8), duration16. Empty slots omitted; section IDs and row references survive compaction.
2. Rows: section ID32, repeats8. Limits64 rows, repeat1..64, duration1..4096 sixteenths.
3. Curated references: length8 and bank key bytes, length8 and part key bytes, group8/kind8/bank-fallback-flag8/part-fallback-flag8. Keys1..95 bytes with no embedded NUL. Resolve stable keys uniquely against the exact catalogue; runtime indexes rebuilt in exclusive staging. No assumption that an array index is a durable song identity.
4. Immutable working parts: revision32, origin reference9bytes, protected-steps64, kind8. Drum payload: length/count/lattice/bars8 each, then count source-ordered16-bit events. Controls, voice set and ordinal indexes are rebuilt and validated without sorting away source ordinal. Dillo payload: length8, then all64 steps with note/gate/accent/slide8 each and expression16. Inactive-step articulation/expression retained. Origin must be a valid same-kind curated source or an earlier working part; forward/self references refuse. Protected bits outside declared length refuse.
5. Complete existing sound/mix/tempo/expression scene as a nested immutable envelope. A required caller-supplied validator must run the real nested decoder and engine admission; NULL cannot bypass it. This codec does not replace current rich/sample/group/expression scene formats.

Bounds:32sections×61 +64rows×5 +96references×196 +16largestDilloParts×407 +3840scene +64header =31504 bytes, below32768. Capacity refusal never trims parts, hits or metadata.

## Snapshot and publication ownership

Encode only a coherent, stopped/suspended main-owned project and a scene captured by the established scene owner. Encode never mutates the project, saved revision or flash; an acknowledged file write/reopen is a separate UI/storage transaction.

Open checks the complete frame/checksum/pins/scene, decodes into caller-owned disposable staging, rebuilds source caches and phrase indexes, then validates the entire reference graph. Failure may alter staging only; it leaves the live project/music and returned nested-view outputs unchanged. Staging must not alias input, output handles or immutable catalogue/cache storage. There is no heap allocation or project-sized stack temporary.

Executable Undo is deliberately RAM-only under the existing arrangement contract. Reopen starts a fresh Undo history and acknowledges the restored revision, preserving all immutable working parts and their source lineage. The eventual import UI must name that history loss and obtain one explicit destructive-project replacement action. It must not publish the model before sound/asset/scene admission and actual stopped restore ownership have succeeded together.

Still required: connect real export/reopen/readback and import/overwrite UI, coherent full scene capture, one acknowledged atomic scene/model publication, optional storage and recovery integration, native fit/stack/timing, and physical persistence/listening. A passing codec is not those outcomes. The current playable localhost preview stays on the previously verified recording build until these controls exist and pass.

## Bounded streaming adapter

The same codec now offers `bb_project_write_stream` and `bb_project_read_stream_staged` alongside the existing contiguous-buffer API. The BBP2 wire format is unchanged. No second serializer, heap or project-sized stack copy is introduced.

The writer accepts separate immutable arrangement/source objects. It writes payload in chunks of at most256B, then writes the final checksum-bearing64B header at offset0 only after all payload writes and revision checks succeed. The adapter must exclusively own a temporary file; callback failure or a changed snapshot abandons it. Publish/rename, close/reopen/readback and `bb_arr_saved` acknowledgement remain the real file owner's responsibility. A successful codec callback is not a saved-project UI claim.

The reader consumes a64B header and at most256B forward chunks. It bounds every count/field while decoding into exclusive caller staging, verifies the CRC of the bytes actually consumed, then invokes nested scene validation and resolves stable source keys. Failure can change disposable staging, never the live project or returned scene handles. A header-valid but corrupt body cannot reach scene admission. No complete input file is required in device RAM.

The arrangement and sources staging pointers may reside in different memory regions. The caller supplies only the nested scene buffer, sized up to3840B. Planned native qualification charges source staging in ordinary RAM and model/scene staging in pool, without borrowing display, USB or active audio storage. The256B codec cache is a small local workspace; complete current-image stack analysis remains required before hardware acceptance.

Callbacks must finish or fail within the file owner's bounded deadline, copy their buffers during the call, never retain those temporary pointers, and never edit music. Opaque callback state must not alias the project or output handles. A cancelled/partial transfer does not publish a project. These cold APIs do not supply that lifetime lock, a USB file protocol, paired-scene snapshot/atomic live restore, browser/device file controls or physical persistence evidence.


## Verified file publication transaction

`bb_project_save_verified` composes the streaming codec with a caller-owned
file backend: acquire a private temporary output, write, close/reopen, confirm
exact file length, validate the complete readback and match its header to the
captured output, then atomically publish. Only successful publication can call
`bb_arr_saved` for the captured revision. Readback of another valid BBP2 file
cannot substitute for the captured output. A failed stage aborts the temporary
file and retains both the named destination and the live saved revision.

The caller still owns a stopped/coherent scene snapshot, exclusive model/source
lifetime, disjoint staging, bounded IO and the backend's publication guarantees.
A failed begin cleans its own partial acquisition; later failures call abort.
A backend must report success only after its required close/rename/sync work;
this helper alone does not prove power-loss durability. A contract-violating
callback that changes revision during publication cannot acknowledge a newer
edit as saved, even if the captured file was already published.

Host tests exercise actual temporary files, close/reopen and rename with the
real C codec, every read/write callback failure, corrupt content, valid-file
substitution, wrong length, revision conflicts, scene-admission rejection and
failed publication. This does not connect firmware file controls, implement a
USB file driver, commit an imported model/scene to live owners, or establish
physical persistence/recovery. The previous native staging-fit result does not
include this new retained transaction entry until separately measured.

## Project edit state (private optional owner)

With `BB_WITH_PROJECT_FILES=1`, the command owner maintains a small saturating
edit-event epoch. It is not serialized and is never a DSP input. It records
admitted persistent core edits, actual group/sample-mix changes, successful kit
changes and accepted manual payload recalls. Common scalar/no-op setters and
rejected commands do not advance it. Notes whose legacy bytes stay unchanged
still count if the operation clears saved expression metadata.

Transport, live pad/key auditions, transient performance transpose/mute bus and
arrangement-derived section playback do not constitute project edits. The
arrangement keeps its own existing model revision. The displayed state is
"edited since file load", not bytewise equivalence: accepted queued manual
recalls remain edited even if a later action cancels them. Undo/reversion does
not silently acknowledge a disk save. Epoch saturation is permanently dirty
until a new scene lifetime is established; it cannot wrap into a clean state.

A completed project import establishes the model/sound baseline only after the
paused scene + model restore succeeds. Boot/reinitialization and an ordinary
scene replacement invalidate that baseline. A failed import never clears it;
a failed partial reset is dirty and remains held. Downloading a snapshot or
saving a legacy sound scene does not acknowledge a complete project file.
A future native verified-save backend must acknowledge both captured model and
sound epochs after publication, while retaining the exact ownership contract.

The feature defaults off. All translation units must use the same setting;
the counter appends a member to the private command-owner ABI. Fresh complete
builds are required, never a mix of cached objects from different settings.
No scene scan, full-core copy or CRC is added to the audio callback for this
indicator. Native timing and complete call-stack acceptance remain separate.
This indicator does not implement the native file backend or automatically
fork live step-editor changes into an arrangement's working-part references.

A completed import also resets navigation focus to drum A before presenting
the arrangement. Keeping an old B/Dillo focus behind a restored A screen can
redirect the next SELECT/PRESETS gesture to another instrument.
