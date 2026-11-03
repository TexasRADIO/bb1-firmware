# BB1 guide source

This directory retains the RC2 guide generator handed over by Claude, so firmware, control maps and documentation can be maintained together. `IMPORT.json` records the upstream Git blob identities.

## Integration status

The generator originated with the earlier RC2 build. Its SLOOP instructions have now been removed, kit counts corrected, and the current 2.1 welcome, combination and five playing-page material incorporated. It is **not release-ready** until the unified release artifacts and browser routes are finalized. Do not publish its output or execute `tools/all.sh`: that historical script assumes a different checkout layout, modifies generator inputs, starts a server and suppresses fit-check failure.

Required integration work:

- Completed: bind captures and key maps to the current composition-21 recipe, with SLOOP disabled and USER enabled.
- Completed: update welcome, preset combinations, all five playing pages and current PRESETS take controls from the actual firmware.
- Completed source edit: remove SLOOP instructions and catalogue claims; disclose removal in What's New. Old SLOOP-save compatibility and migration are explicitly out of scope by Joe's decision. Rendered-output verification remains pending.
- Completed source alignment: bind installer instructions to the verified app and sound-library pair, with switching, return and help paths. Final staged release-route verification remains open.
- Point browser links to the matched 2.1 player; keep first-PLAY sound enablement and phone controls documented.
- Replace historical orchestration with an explicit, failure-propagating build in an isolated output directory.
- Render and inspect desktop, phone and print output; repair publication-origin PDF links and check every chapter anchor.

Firmware/host checks do not establish physical UI, listening or timing acceptance. Publication remains a separate release step.


## Current capture check

`tools/build.sh` now takes feature definitions from the current composition-21 recipe and writes its generated prelude into the output directory. The retained `release_flags.txt` is historical provenance and is no longer used. The no-SLOOP kit fixtures show 12 KIT A entries and 8 KIT B entries. The latest current-tree host capture generated 89 frames with zero layout-lint failures at `/Volumes/Orange Base/Codex Working Cache/tmp/bb1-21-manual-capture-current-20261103`, including the approved Rhythm Conductor mark, instrument-first landing, combination flow and five playing views. This is fixture rendering, not physical-device or complete human visual acceptance.

## Complete guide build audit

The current generator emits a 16-chapter review guide and 424 trace rows. Source identities come from the actual input trees, with dirty/unversioned inputs explicitly marked. Missing source files or patterns are collected, and unresolved references make the build fail instead of reporting success. The latest current-tree review is at `/Volumes/Orange Base/Codex Working Cache/tmp/bb1-21-guide-current-20261103/`; all 424 source references resolve against the firmware, catalogue, key map and staged 2.1 installer. Startup wording distinguishes the welcome card from the full song list, and the control table now uses the Rhythm Conductor name. The obsolete external RC2 keymap SVG and stale PERFORM index entry were removed in favour of the current-tree role and lighting tables. `tools/check_links.py PUBLIC_ROOT` verifies guide-local images, files and anchors: the 18 HTML documents pass 1,111 reference checks. True 390-pixel iframe audits of the guide, chapter 9, player and installer report no document overflow; wide guide tables retain their intentional local scroll. The 68-page A4 PDF review has no forms, scripts, encryption or sampled visual defects. Physical-device acceptance remains separate. Image dependency used: Pillow 12.3.0 in the bundled Codex runtime.
