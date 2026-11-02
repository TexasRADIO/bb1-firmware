# Current private 2.0 lean native profile

This profile supersedes the historical `composition-20` cost recipe for the current project-file-enabled build. It uses the same 66 source units, flags, linker protections, audio quality and limits as the qualified 0ec0915 build. All project streaming, verified-save, ownership, arranger, working-part fork and live-recording APIs remain retained.

Three native-unused adapter roots are omitted: `bb_project_encode`, `bb_project_open_staged` and `bb_live_record_begin`. These adapters remain available in source for host/WASM and compatibility tests. Native project ownership uses the streaming codec and recording uses the phase-aware entry point. No user-facing feature is removed. Do not remove the remaining qualification roots just because a platform file driver is not yet connected.

Fresh native comparison: 580544 -> 580240 bytes, saving 304 bytes; code free 1020 -> 1324 bytes. Ordinary RAM free 9284, pool free 30852, fast-memory text 24028 bytes, all unchanged. All 66 input object files are byte-identical between profiles. Earlier trimming separately saved 1616 code bytes and 5984 RAM bytes; those figures are historical comparisons, not a total current-size reduction against 1.5.

The exact recipe retains disabled-provider guards for parts, prepared banks, Telegram, 909 routing, profiling and the owner/audio bridge. Enabling those profiles requires restoring their providers and qualification. Startup/IRQ/CPU1/static-storage protections are retained. No licensing, provenance or compatibility source is deleted.

Use the source-tree `cmds-r4-onepass` command set with this recipe. The measured build used the retained `project-stream-native-cmds` copy; its 66 unit command inputs were compared with the source-tree command set. Audit the fresh objects with `tools/audit-linked-build.py --build OUTPUT --recipe RECIPE/recipe.json --output REPORT.json`.

Host validation: 539-section actual-C audio oracle matches the expected digest; 4171 verified file-transaction checks pass under ASan/UBSan. Reachability covers 66 objects, 1636 allocated sections and 9355 relocations, with no zero-retained module or unreferenced fast-memory section. Static reachability is not proof of indirect runtime paths, whole-stack safety or hardware timing. No FWSC was produced and no device was contacted for this trim.

## Complete profile guard

The lean recipe now validates all 62 C units before compiling any object. The four startup/vector/IRQ/probe assembly units are explicitly exempted; an uncovered or duplicate unit is rejected. Fourteen required disabled/ABI feature flags are checked after command rewriting, including project ownership, capture and UI-layout flags. `python3 big-beat/integration/tests/test_build_profile.py` exercises the actual builder functions against the real command set: 1743 checks cover every guarded unit/flag, undefined flags, conflicting requirements, missing units and the historical two-unit recipe policy. This is a build configuration check, not a timing or hardware result.
