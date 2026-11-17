# Big Beat Studio BB1 2.3 native Firmware Hub release

[Download v2.3-editor.2](https://github.com/TexasRADIO/bb1-firmware/releases/tag/v2.3-editor.2) · [Play in your browser](https://thebigbeat.app/fm1/emulator/) · [Player’s guide](https://thebigbeat.app/fm1/guide/)


The release retains BB1 firmware version 2.3 and the shared-editor protocol.
Its release tag is `v2.3-editor.2`.

The `.fwsc` is now a self-contained Firmware Hub package. One native Hub
install writes the unchanged 2.3 application and its exact matching 240 KiB
sound library. The loader writes only the application range
`0x4000..0x93000` and the library range `0xA0000..0xDC000`; it does not write
the flash head, bootloader or other storage regions.

The package was verified on a physical M-VAVE FM-1. The unit booted as
`FM-1_92200203`, reported a known flash device and clean runtime status, and
the installed `BBSK` library header matched the released library. The test
unit was then restored to its prior firmware using its verified return
package.

Corresponding source for the BB1 2.3 shared-editor package identified in
`RELEASE-BUILD-PINS.json`. This is the retained native-build source snapshot;
its content hash identifies it independently of a Git commit.

## Native application build

The JieLi PI32v2 toolchain is an external dependency. Its executables run on
x86-64 Linux. With that toolchain available at an absolute path, run from the
extracted source directory:

```sh
JIELI_TOOLCHAIN=/absolute/path/to/jieli-toolchain python3 build23.py
```

On other hosts, run the same command in an x86-64 Linux environment with this
directory mounted read-write and the toolchain mounted read-only. The script
uses the retained per-unit compiler commands, size pass and link order, writes
`build/app.bin`, and refuses success if its hash differs from the release pin.
The existing target and package hashes were verified from retained build
outputs; an independent source rebuild was not repeated while preparing this
archive. `tools/fm1pkg_make.py` packages an application with its matching RAM
loader and product identity. The released package includes its matching
full-install loader. The package and loader can be independently checked with
`tools/verify_hub_install.py` and
`big-beat/integration/tests/test_hub_loader.c`.

The shared editor exposes the current firmware's bounded working-state
surface. Editor state files are distinct from full device projects. Native
project transfer and device-originated watch support must not be inferred
from the existence of the two-way editor protocol.

## Licence

Original firmware is GPL-3.0-only. See `LICENSE`, `LICENSING.md`,
`BIG-BEAT-LICENSING.md` and `LICENSES/`. Third-party components and the
separately supplied sound library retain their own terms.
