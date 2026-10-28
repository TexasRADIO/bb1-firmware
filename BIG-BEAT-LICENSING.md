# Big Beat licensing and provenance addendum

This file supplements Felucca's `LICENSING.md`, which covers the Felucca firmware, tools, web pages and the third-party parts it lists. It covers what Big Beat adds on top. It ships at the root of the corresponding-source bundle (`package/assemble_source_bundle.py`).

## Firmware code

| What | Licence | Where |
|---|---|---|
| Big Beat firmware changes: audio engine, player, catalogue loaders, v3 UI, navigation, CPU1 worker, Route A, scan reduction | GPL-3.0-only (as Felucca) | `firmware/` in the candidate tree; the r9 patch series is in `big-beat/integration/r9-step1/patches/` |
| 9W9 TR-909 voices: a port of 9W9's `er99_engine.c`, `er99_circuit.h`, `er99_tom909.h` and `er99_perc909.h` (circuit models, fitted constants, defaults) | GPL-3.0 | `firmware/src/drum909_group.c` and related `bb909*` sources; licence text in `LICENSES/909-reference-LICENSE` |
| The native 808 voices, Dillo and Telegram synthesis | GPL-3.0-only | `firmware/src/bb/` |

## Sampled kits and pattern data

**Owner authorisation (28 October 2026).** Joe, the project owner, said in the Big Beat development thread:

> Big Beat may redistribute the LinnDrum, DrumTraks and DMX samples and pattern data.

The quote was recorded on GitHub on TexasRADIO/big-beat-fm1-review issue #4, comment 6461151035.

| What | Covered by | Provenance record |
|---|---|---|
| LinnDrum, DrumTraks and DMX samples in the sound library (`f4bbf1a4…`, 4-bit IMA ADPCM at half rate) | Owner authorisation above | `release-sample-provenance-r1.json` (SHA-256 `829393ee84185dc55824bb47580fa962c59f3f58d6021762556b06fd291b62f3`): every voice's source WAV hash through encoding, from the "Drum Machines Collection" |
| Pattern data in the catalogue: approved Big Beat songs and eras, Dillo and Telegram lines, era drums, and the Roland factory-preset shelves (CR-8000, CR-78, TR-66, TR-77) | Owner authorisation above | Catalogue generator `claude-code/r9/catalogue/` at `75dd228`, with its pinned inputs (`out/inputs.json`); Roland selection in `claude-code/integration/shelves/roland-factory-138.json` |

**What the authorisation does not establish.** As recorded with the authorisation, it does not independently establish historical authenticity, source-machine identity or third-party provenance. The ledger's `historicalAuthenticityVerified:false` stands.

## Not in the bundle

- The JieLi AC79 SDK files inside a `.fwsc` package: Apache-2.0, from the public SDK (see `LICENSING.md`).
- The Jieli compiler toolchain: pinned by hash in `big-beat/integration/inputs.json`. Its redistribution terms are not yet confirmed.
