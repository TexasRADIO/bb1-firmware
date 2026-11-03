# BB1 browser firmware build

This adapter runs the firmware UI and audio code in WebAssembly. It replaces target assembly and panel SPI access for the host environment. It is not evidence of native timing, IRQ behaviour, USB transport or physical acceptance.

The starting adapter and support files came from the verified public BB1 2.0 corresponding-source bundle. `IMPORT-MANIFEST.json` records their original hashes before these adaptations. Preserve upstream firmware and component copyright and licence notices; this directory does not relicense them.

Build from the repository root with Apple Clang supporting wasm32 (the tested compiler is Apple Clang 21):

```sh
python3 big-beat/emulator/compile.py "$PWD" /path/to/fresh-output
WASM_LD=/path/to/wasm-ld-14 sh big-beat/emulator/link.sh /path/to/fresh-output
```

The compiler uses the composition-21 unit list and the current release feature switches (USER kits enabled, SLOOP removed). Product and display identity come from `big-beat/identity.json`. The adapter calls the real firmware drawing code. Supply the matching sound library to `emu_sound_window()` before calling `emu_init()`.

The existing offline Linux toolchain can perform the link when wasm-ld is unavailable on macOS. It needs only read access to this directory and write access to the output directory; no network or instrument access is needed.

A successful compile/link is not release acceptance. Verify module imports/exports, identity, library loading, UI controls, rendered pages, audio and project operations before publication. Keep the native firmware, this module, library and website wrapper pinned together. Runtime feature checks and browser journeys remain necessary beyond the native host fixtures.

Stage the browser player from the same module and validated library (new output directory required):

```sh
python3 big-beat/emulator/package_player.py /path/to/bigbeat.wasm /path/to/bb1-sounds-2.1.bin /path/to/player --muted
python3 big-beat/emulator/test_package_player.py /path/to/bigbeat.wasm /path/to/bb1-sounds-2.1.bin
```

The packager reads product identity from the actual module, verifies the full library and source help pins, embeds the module deterministically and records hashes. `--muted` starts the browser master at zero for QA; omit it for the intended player default. This stages files only. Publication and hardware acceptance are not implied. Template provenance is retained in `PLAYER-IMPORT.json`.

The web panel reads full, dim, mid and breathing intent from the firmware through `emu_led_state()`. CSS provides visible state distinctions, not a calibration of physical PWM brightness. Reduced-motion users see steady breathing indicators. The old full-lit pointer alone is insufficient.

`test_lights.mjs BEFORE_MODULE AFTER_MODULE LIBRARY` compares actual audio and framebuffers across a control journey while observing LED states. Remaining work includes full desktop/mobile navigation and project journeys and adopting the exact package into the website release line.

Current-version composition persistence regression:

```sh
node big-beat/emulator/test_project.mjs MODULE LIBRARY NEW_OUTPUT
```

It creates and edits a two-row song through the firmware controls, roundtrips the snapshot into a fresh module, verifies dirty tracking/reopen and refuses damaged files without mutation. Export resume briefly refuses edits until its audio commands drain; the test checks this refusal and subsequent successful editing. This does not prove OS download persistence, browser file-picker behavior, hardware saves or old SLOOP compatibility.
