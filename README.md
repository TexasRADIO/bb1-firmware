# BB1 2.3 shared-editor firmware

[Download the editor release](https://github.com/TexasRADIO/bb1-firmware/releases/tag/v2.3-editor.1) · [Play in your browser](https://thebigbeat.app/fm1/emulator/) · [Player’s guide](https://thebigbeat.app/fm1/guide/)


The editor release retains BB1 firmware version 2.3. Its release tag is `v2.3-editor.1`.

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
loader and product identity. The released package includes its matching loader.

The shared editor exposes the current firmware's bounded working-state
surface. Editor state files are distinct from full device projects. Native
project transfer and device-originated watch support must not be inferred
from the existence of the two-way editor protocol.

## Licence

Original firmware is GPL-3.0-only. See `LICENSE`, `LICENSING.md`,
`BIG-BEAT-LICENSING.md` and `LICENSES/`. Third-party components and the
separately supplied sound library retain their own terms.
