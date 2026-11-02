# BB-1 2.0 build

## Selected source and status — 2 November 2026

The release firmware source is `8425ff5db5da689da7a69b161c1e66a8b7974627` (`freeze/2.0-r3`). It supersedes the pre-fix `732646dd` freeze. The section-owner correction preserves sixteenth-note swing parity across odd-length SONG handoffs.

Published at the owner’s explicit direction with the recorded timing issue deferred to a minor follow-up. Whole-stack and exhaustive hardware/persistence qualification are not represented as completed passes. See the release notes for exact known limitations.

Runtime identity is `FM-1_92200200`, display `BB-1 2.0`. Historical alpha descriptions and initial foundation sizes are not the current build.

## Native build

Use the pinned toolchain and external dependencies documented in `BB1-1.5-BUILD.md`. No proprietary toolchain is redistributed. Build the selected source with the **composition-20-sloop** recipe:

```sh
bash big-beat/integration/build/run_build.sh SOURCE \
 SOURCE/big-beat/integration/build/cmds-r4-onepass \
 SOURCE/big-beat/integration/build/recipes/composition-20-sloop NEW_OUTPUT
```

Coordinate the existing native compiler lease before running. Use a fresh output directory and retain the recipe, flags, command logs and linker map with the resulting artifacts.

The independent 71-object build matches the lead's app exactly:

| Artifact or measurement | Frozen candidate |
| --- | --- |
| App | 568,548 bytes |
| App SHA-256 | `4e083d1d7e9af457f93b0218837fed2b5ad2608b99a97968d58df020d5fd9977` |
| FWSC SHA-256 | `30cfd720da4b9c02121deb2c9b90f1fedcf00c4cfe149544c1b1505f99c478d1` |
| Code free | 13,016 bytes |
| Ordinary RAM free | 4,212 bytes |
| Pool free | 13,096 bytes |
| RAM text | 24,028 bytes |
| Matched library | 1.4, SHA-256 `9911a933e25a4bd507c4fa8240f86d53785eeb5ca6654e4e51215077ed447ada` |

Native fit is not a runtime timing or stack-safety qualification.

## Browser module

The staged release module is 1,226,390 bytes, SHA-256 `4ee2dc6b6bb83912cdc765b7a36e84c1f988b35b323c1c76dc10c90e94f29285`, with 71 exports and no imports. It uses the frozen firmware plus the BB1 release adapter, host preprocessing, freestanding support and shims. The upstream `web/emu` Felucca adapter alone does not reproduce this module. The [corresponding BB1 source and build-input bundle](https://github.com/TexasRADIO/bb1-firmware/releases/download/v2.0/bb1-emulator-source-2.0.zip) includes that exact firmware archive and the adapter inputs. A fresh rebuild reproduces the staged module byte for byte with the recorded tools; see its `CLEAN-REPLAY-RESULT.json`.

The browser adapter replaces target assembly and panel SPI access with host equivalents. Browser results do not establish native timing, USB transport, physical controls, persistence or whole-stack acceptance.

## Qualification scope

The frozen correction passed 16,441 scoped audio/mode checks and 472 tap/navigation checks. Keep the 539 traditional sections, 205 ACID phrases and 168 legacy scenes as distinct coverage sets; do not treat historical foundation totals as a fresh complete run on every linked image.

The app-only 2.0-to-1.5 round trip passed with two final reads matching all four original regions. The later 2.0/library-1.4 session showed a 6,031.5 microsecond callback against a 5,802.667 microsecond deadline, with one deadline exceed and one DMA miss. Subsequent 20 separate capped 30-second exposures passed without reproducing the miss. Those separate runs do not establish continuous exposure or explain the original event. Profiling and a timing fix are follow-up work; measurement firmware is not part of this release.

Preserve component source and licence notices, original musical quality, two independent drums, Dillo, echo and spring. Physical, persistence, timing and stack verification remain separate from these build instructions; publication does not turn unverified work into a pass.
