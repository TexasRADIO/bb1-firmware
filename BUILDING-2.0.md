# BB-1 2.0 development build

Joe assigned Duke to design, implement and assemble2.0 while Claude is offline, with subsequent review. This checkout is an isolated development branch from exact published corrected-ID1.5 source `b0b9c01acd1edeccf25fc35716f9f29d424a59c5`. It does not change live1.5, the historicalv1.5 tag or the1.1.1 publication hold.

## Current lean build (2026-10-31)

Use `big-beat/integration/build/recipes/composition-20-lean` for the current private build with project ownership enabled. See that recipe's README for the audit, preserved APIs and measured savings. The older foundation costs and implementation descriptions below are historical. This trim was freshly compiled and host-tested; it has no new hardware acceptance.

## Alpha1 foundation actually assembled

- Tempo display and first GLOBAL edit correction.
- Later MIDI: Continue/resume distinct from Start/reset; default-OFF USB24PPQN output; source-owned bend/sustain and scoped bank/program selection. Not external phase-sync or general THRU.
- Rich-owner Euclid/Random and reversible generator handoff, retaining curated source data and metadata. GPL source credit in bb_gen.c is retained.
- The original1.5 two drum machines, curated catalogue, Dillo/Open303, echo/spring and Armadillo foundation remain. Telegram/FixA stay OFF; no new engine, RAM-text expansion, quality reduction or increased join/deadline bound.

Internal development identity: `FM-1_92200200`, display `BB-1 2.0 alpha 1`. This is a development marker, not a published release or globally reserved Hub identity. An offline FWSC and a short scoped unit check are now qualified; see HARDWARE-2.0-ALPHA1.md. This remains an incomplete development build, with no stable-release or full device acceptance.

## Native build

Use the unchanged1.5 pinned toolchain/Docker build process and external dependencies documented in BB1-1.5-BUILD.md. Every object compiles from source. The composition-20 recipe adds bb_gen in the recorded link order and its compile command derives from bb_evolve, changing only unit paths. It uses the same linker/RAM-text boundaries and default audio quality. No proprietary toolchain is redistributed.

```
bash big-beat/integration/build/run_build.sh SOURCE \
 SOURCE/big-beat/integration/build/cmds-r4-onepass \
 SOURCE/big-beat/integration/build/recipes/composition-20-lean NEW_OUTPUT
```

Coordinate the existing atomic native compiler lease before running. The exact initial build used BB_WORKSPACE=/Volumes/Orange Base/Codex Working Cache/tmp/dillo-duo-revamp-20261029/native-workspace. Native output and host evidence are retained alongside this checkout under /Volumes/Orange Base/Codex Working Cache/tmp/bb1-20-integration-20261030.

Initial measured combined result:76fresh objects; app554144B, SHA4acc20a98b6cbb3246ef786c720414f85032e8ed7d4ee63f23ef3bc3fd0354e0; codefree27420B; ordinaryRAMfree31364B; poolfree77204B; RAMtext24028B. These are measured combined costs, not sums of separate patch estimates. Native fit does not establish runtime timing or stability.

## Current tests and unfinished implementation

Actual combined C engine catalogue oracle:539sections/482470calls; PCM hashdd330af1fdb22d765c8e53d509d24ede6ef779934875b0cf55931a9011716ab6, identical to released1.5 with default musical settings. Actual MIDI/transport owner58105checks; generator2551298pure+8alias checks; rich-owner6004checks across539sections. ASan/UBSan enabled. These do not establish panel usability, real receiver clock jitter, persistence, native playback timing or listening.

The named arranger, per-hit expression/independent lanes, complete musical Undo/capture/project codec/MIDI export/sync, hybrid-kit production migration/installer and remaining measured engines are not implemented by this foundation. The prior host arrangement document model is not a playable arranger. Follow big-beat/composition-20/PLAN.md and ARRANGER-CONTRACT.md for the real C owner/scene integration; all six user priorities remain in scope. Full WASM/UI integration, complete combined native/stack/performance qualification and physical recovery/listening follow implementation. No completed2.0 or ready-to-publish claim is made.
