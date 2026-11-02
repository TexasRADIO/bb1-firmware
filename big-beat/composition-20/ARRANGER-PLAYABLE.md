# Named arranger: first playable integration

This development implementation connects named-section authoring, the real
catalogue/working registry, automatic row/repeat transport and an actual FM-1
UI page. It retains the two existing drum renderers, Dillo and shared echo/spring.
It is not yet a stable/public2.0 release or a full project-storage implementation.

## Musical ownership and transport

A bounded registry stores actual catalogue bank/part keys plus the validated
catalogue CRC/stamp. Numeric node/array offsets are runtime caches only. Each
resolution checks its identity and revision; missing, ambiguous, overlong,
wrong-kind or changed sources are refused. Source ordinals, rich articulation,
accent controls and Dillo expression are never projected into a simpler grid.
The immutable16-slot working registry explicitly forks complete payloads,
retains source lineage/protected-step metadata and does not silently evict.

The main-loop controller prepares whole A/B/Dillo sections outside audio,
then supplies absolute musical tick targets to the qualified atomic owner.
The owner records entry ticks, applies duration×repeat and stops at the exact
end if no successor has been accepted. It does not emit an extra final note.
A finite arrangement, loop, stopped load, one-shot cue/return, pause/Continue
and single-command section+Start all use the actual engine. Start reuses the
existing owner transport side effects; no separate STOP/START queue pair or
new synthesis path is introduced. End boundaries are included in the existing
P1b worker exclusion, preserving quality/deadline/join bounds.

The core now explicitly accepts a qualified STOP in its before-tick hook:
it renders remaining tails and retains the un-emitted boundary for Continue.
Ordinary playback, with no scheduled ending, retains the previous path.
Target MIDI Start can delegate to the active program through a main-loop
callback, after the existing source/settings checks. Continue retains the
paused position. This does not establish external phase/jitter/clock-loss
acceptance, source-aware THRU or the later complete sync/export feature.

Only matching owner acknowledgements advance PLAYING/NEXT. Independent
instrument edits or unexpected model changes invalidate program ownership.
A selected row is not an applied row. Creating a named section plus its row
is one reversible transaction; no partial Add or two-press Undo is implied.

## Physical controls in this development build

Open GLO, then white key4. This uses the same GLOBAL subpage convention as MIDI.
HOME/GLO leaves the page and keeps its RAM project; playing may continue.

- K1/SELECT/PRESETS selects a row. K2 changes its repeat; K3 changes the shared
  section duration in sixteenth units; K4 chooses song loop on/off.
- White1/2 opens sources. There, K1 selects A/B/Dillo, K2/SELECT selects a group,
  ALGORITHM selects bank, K3/PRESETS selects part and K4 sets section duration.
  White3 explicitly includes/omits Dillo. EDIT adds the complete section.
- White3 on the row page copies a row with a clearly shared section identity.
  White4 requests removal; the confirmation names its target and binds the
  current revision. EDIT confirms. HOME exits without deletion.
- Black1 Undo / black2 Redo retain the composition history across focus changes.
  Ordinary authoring edits are currently stopped-only; live cueing is separate.
- PLAY starts the whole song from row1, or stops. EDIT loads the selected row
  while stopped, or cues it once at the current section end while running.
  A live cue then returns to the original next row without changing the model.

The shared v3/Armadillo page shows LOADED/PLAYING, NEXT, the selected row and
UNSAVED separately, with visible knob assignments and matching action LEDs.
Its dirty regions are bounded lines/rows, rather than one large body redraw.
SAVE says the project is RAM-only and does not pretend the legacy scene stores
its arrangement. A scene restore releases performance ownership but retains
this RAM project; no new flash write/layout or original-scene overwrite exists.

## Remaining work

This is the first connected arranger, not the entire six-priority2.0 brief.
Arbitrary-name input, moving rows and Make-independent are available at model/
registry level but not all exposed on this first page. Live program editing,
acknowledged musical-object Undo, clipboard/capture, lane expression/ratchets,
seeded protected Evolve/fills, versioned complete project codec/restore/export,
full sync/THRU, kit migration/production installation and the other measured
engine work remain active. Capacity errors must remain explicit throughout.

Qualify the final frozen source with actual-engine tests, full native resources
and stack, unchanged-default PCM/MIDI/UI regressions, full WASM/rendered preview,
then exact on-unit boundary timing, controls/listening/persistence/recovery.
Historical alpha1/section-owner fits or short unit tests do not qualify this
new integrated source. Public1.5 and its rollback remain unchanged.
