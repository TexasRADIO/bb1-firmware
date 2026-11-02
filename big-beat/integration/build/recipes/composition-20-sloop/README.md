# BB1 2.0 release-candidate composition

Use the existing `composition-20-sloop` recipe and `run_build.sh` assembly path. This recipe keeps the lead's ordered 71-unit link, compiler/quality flags, roots, linker/startup protection, two drum layers, Dillo, echo/spring, SLOOP, enabled USER builder and shared scene/project implementation. It changes no voice cap, sound, clock or DSP path.

`BB_WITH_SLOOP_KITS=1` enables the feature; **`BB_SLOOP_TARGET_BUILD=1`** explicitly selects its target profile. The latter is a build configuration opt-in, **not a statement that runtime, stack, recovery or release acceptance has passed**. An absent/zero value fails a target build; host serial-only tests remain available. The old `BB_SLOOP_NATIVE_MEASURE` flag is no longer the current-source opt-in. Historical source/recipe receipts retain their original flags and identities.

## Matched assets and provenance

The lead adopted SLOOP plus help-in-flash at00331587c8d31990fb9d74696e7db6bcc7e04674 (#64 6558661857). The2.0 library name is **bb1-sounds-1.4.bin**: BBSK in A, BBTX at0xD7000..0xD8FFF, BS31 at0xD9C00..0xDBFFF. USER1–4 use the separate lead-confirmed slot0xDE000. No regionB ownership is added by this recipe.

For that exact text conversion, library1.4 SHA256 is2da6381e4132e8b201469c114d775ae4c2b23a39da7ee43e07a7cad8cb706bc3. A later D4/text conversion changes the matching block/library hash: freeze bb_text_block.def with R2 and take final app/source/library hashes from R9. Pair the exact app and library in installer, emulator and release table. Preserve the exact1.5 return library1.2/154ef6b9;1.3 remains an internal composition step. Missing/stale text falls back; this is not an accepted substitute for shipping the matched library.

Retain SLOOP2.8.3 source/recipe pins, GPL-3.0-only source and original credits; preserve other sound/source licence and attribution records. No sound-quality reduction is part of this profile. Existing limits remain A-alone2, bothSLOOP1each; B-only policy outside the approved combinations still needs lead disposition.

## Acceptance and release sequence

Build with the existing command: `big-beat/integration/build/run_build.sh SOURCE_TREE CMDS_DIR RECIPE_DIR OUT_DIR`, where CMDS_DIR is big-beat/integration/build/cmds-r4-onepass and RECIPE_DIR is this directory; OUT_DIR must be new. See the source build instructions for pinned tools/container. Ordinary RAM floor4096B, pool8192B and code0B remain unchanged. Do not infer native timing, whole-stack/IRQ/CPU1, listening or physical persistence from a successful link.

This is the release build recipe handed to the integration lead, not a code freeze or authority to install/package/publish. Finish locked scope and final combined fit, then the lead's R2–R10 gates and exact frozen source/bundle reproduction. Device work additionally requires R1 and Joe's standing rule: a proven same-Mac/tool return; reachable runtime identity/diagnostics/bounded STOP/masterzero/BB1 MIDI updater; held /private/tmp/bb1-device-session.lease; exact corrected-ID1.5 app/user/A/B return. ROM alone does not qualify. Publication follows final explicit approval.
