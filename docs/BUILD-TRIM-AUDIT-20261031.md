# Private 2.0 build and unused-code audit

Audited input: source 0ec0915ff428813cb547374345698cdfcdb33b80, native-linn-label-final. This is the complete current native build's dependency/retention audit, not a claim of an exhaustive security review or physical acceptance.

## Changes

The current build already excludes 17 inactive provider modules and 27 earlier forced roots. Their prior measured trim saved 1616 code bytes and 5984 ordinary RAM bytes. Do not count those as a new saving in this pass.

This pass removes three additional forced native adapters: whole-buffer project encode/open and the integer-grid recording wrapper. Current firmware uses streaming project operations and phase-aware recording. Host/WASM compatibility APIs and their tests remain in source. Working-part fork, verified file publication and owner transaction APIs are deliberately retained, even where future platform IO is still needed. Nothing deletes historical source, attribution, rollback or scene decoders.

Added composition-20-lean as the documented current native profile and tools/audit-linked-build.py as a repeatable conservative object-section audit. It rejects object directories that do not exactly match the recipe's unit list. Every recipe root is treated as required; a prior audit helper's older hard-coded API list omitted newer project-owner roots and would falsely classify required state as orphaned. That classification is corrected here.

## Measured result

| Resource | Before | After |
| --- | ---: | ---: |
| Application bytes | 580544 | 580240 |
| Code free | 1020 | 1324 |
| Ordinary RAM free | 9284 | 9284 |
| Pool free | 30852 | 30852 |
| Fast-memory text bytes | 24028 | 24028 |

Additional saving: **304 code bytes**, no RAM saving. Native SHA-256: cdf39753cc7326d20796b54513e5f709d04f4d18fdb9f4f2dc5de5260470b0ce.

## Checks

- Full fresh native build: 66 objects; all 66 byte-identical to the baseline objects. Only linker roots change. All retained API roots appear in the final symbol table; the three removed adapters do not.
- All 67 recorded compile stages match the documented source-tree command set.
- Dependency audit: 1636 allocated sections, 9355 relocations, no invalid relocation indices; all 66 modules retain code. No unreferenced fast-memory section found under the conservative graph. Protected startup, vectors, interrupts, CPU1, static storage and current recipe APIs are preserved.
- Actual-C catalogue oracle: 539 sections, 482470 calls, no failed calls; expected audio digest dd330af1fdb22d765c8e53d509d24ede6ef779934875b0cf55931a9011716ab6.
- ASan/UBSan project/file suite: 4171 verified transaction checks pass, including every injected failure in 50 writes and 50 reads, close/reopen and rename.
- Source inventory includes 404 C/assembly/header/include files. An uncompiled file or a file included through another translation unit is not automatically dead source. Inactive engine code is excluded from this profile rather than deleting reusable/licensed source indiscriminately.

Raw evidence: /Volumes/Orange Base/Codex Working Cache/tmp/bb1-20-integration-20261030/trim-current-20261031/. RESULT.json compares native resources and object hashes; BASELINE-AUDIT.json and LEAN-AUDIT.json contain graph results; oracle/ and PROJECT-TEST.log retain fresh host outputs.

## Remaining limits

Only 1324 code bytes remain. Further substantial savings need deliberate redesign or a feature/profile choice; this audit found no additional wholly unused module or fast-memory function to remove safely. The linker conservatively retains some static storage and all explicit qualification APIs; reachability is not runtime liveness proof. Whole-stack safety, native audio timing and physical usability remain separate unfinished gates. No FWSC, device write, public release or preview replacement was performed by this audit. The existing browser preview remains on its previously tested build.

## Profile guard follow-up

The old guard checked only main and bb_audio, so an incompatible flag in another C unit was not rejected by that guard. The current lean recipe now checks 14 explicit shared-profile/provider flags in all 62 C units. Its four assembly files have named exclusions; missing/duplicate/unaccounted units and conflicting requirements fail. Legacy recipes retain their two-unit policy until explicitly migrated.

1743 actual-function tests pass against the recorded command inputs, both with normal Python and Python -O. A real pinned-container negative run with BB_WITH_TELEGRAM=1 injected only into bb_phrase_shaker refuses before any compiler command/object is written. The positive full native rebuild produces the same 580240-byte application and all 66 identical objects. No musical source, recipe flags, device or public release changed. These are explicit-command-profile checks; they do not prove every possible preprocessor/header-derived ABI configuration or runtime behavior.

Follow-up raw evidence: /Volumes/Orange Base/Codex Working Cache/tmp/bb1-20-integration-20261030/trim-profile-guard-20261031/.
