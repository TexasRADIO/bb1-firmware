# From-source FM-1 build (integration lead)

One script builds the complete app image from a source tree. **Every object is compiled from source;**
no object is reused from an earlier build. The recorded compiler command lines are replayed, with only
their path prefixes rewritten.

## Result: boot42 reproduced byte for byte

| | |
|---|---|
| Source | `/Volumes/Orange Base/Codex Working Cache/tmp/bb-silent-b-candidate-r1` (installed boot42); `firmware/` + `build/` tree SHA-256 `81539f9b63d54e138d1177b473aaec2e4e11fe36d34ad68f084f082fdebc8dab` (441 files, sorted `path sha256` lines) |
| Commands | `cmds-r4-onepass/`: 75 unit commands + `main-front`, copied from the boot36-lineage full build `sample-audio-r758/load-r4-onepass-target-r1` |
| Recipe | `recipes/boot42-repro/recipe.json`: 75-object link order, the 43 boot42 roots, `guarded-gc.ld` (run-r3), the 909 asset symbols |
| **App** | **`27e6c75b3508c4df89042481f2425f109f27ce0fd63a06f28499dc29efe44982`**, 552,836 B: identical to the installed boot42 (`verification/target-r1/candidate-report.json`) |
| Fit | 28,728 B code free, 8,576 B RAM free, 81,776 B pool free, 24,028 B RAM text |

Boot42 itself was not built this way. It was linked from retained boot41-lineage objects that came from undocumented mounts (`/audio`, `/pruned`). The pinned driver used for earlier review builds also needs `experiments/bb-control-feedback-20261016/target-r3`, which disappeared from Orange Base on 27 October. This script needs neither.

**One recorded exception.** Boot42's `main-size.ll` leaves three `gfx.c` helpers at `-Os` (`cv_line`, `cv_line_t`, `cv_text_fit`), but the size pass marks them `minsize`. This is the only difference between the two marking passes; the IR is otherwise identical. The reproduction recipe lists them in `size_exclude`. Without the exception, the image differs from boot42 in 11 bytes of one inlined function (`u_line`), with the same size and every symbol at the same address. Candidates built from here on may drop the exception.

## Run

```bash
claude-code/integration/build/run_build.sh SOURCE_TREE claude-code/integration/build/cmds-r4-onepass RECIPE_DIR OUT_DIR
```

- Runs in the pinned image `sha256:2d0e361d…afac3` with the Jieli toolchain `jieli-linux-toolchains-20260730.1`, network off and every input read-only.
- Needs Docker and the Orange Base toolchain, linker overlay and 909 asset folders (paths in `run_build.sh`).
- Writes `OUT_DIR/report.json` (per-unit source and object SHA-256, image hash and fit) and `OUT_DIR/candidate/` (`app.elf`, `app.bin`, `symbols`).
- Never packages, installs or contacts a device.
