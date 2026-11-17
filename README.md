# BB1 2.3.1

Maintenance release for the M-VAVE FM-1, product `FM-1_92200231`.

[Release downloads](https://github.com/TexasRADIO/bb1-firmware/releases/tag/v2.3.1) · [Install BB1](https://thebigbeat.app/fm1/install/) · [Player’s guide](https://thebigbeat.app/fm1/guide/) · [Browser editor](https://thebigbeat.app/editor/)

BB1 combines two drum layers, Dillo bass and effects. This maintenance release adds a
read-only runtime report of sampled-kit admission and audio-fault state.
See `RELEASE.md` for the report and its limits.

## Build the application

The current identity and output hashes are in `RELEASE-BUILD-PINS.json`.
Use the active release pins:

```sh
JIELI_TOOLCHAIN=/absolute/path/to/toolchain python3 build23.py --pins RELEASE-BUILD-PINS.json
```

The toolchain runs on Linux x86-64. Mount this extracted source read-write,
the pinned toolchain read-only, and disable networking when building in the
container image specified in `EXTERNAL-BUILD-INPUTS.json`. Python 3 and the
toolchain are sufficient for this application build; generated inputs are
included. Compile errors, UI size-pass failures, resource overflow and a
different application SHA256 cause the build to fail.

## Build the RAM update loader

From this same directory and Linux environment:

```sh
JIELI_TOOLCHAIN=/absolute/path/to/toolchain python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.build_loader()'
```

This calls only the loader compiler and wrapper. It does not invoke the full
upstream build or any device installer. Expected `build/loader/ota.bin` is
7001 bytes; its SHA256 is recorded in the release pins.

## Package and external inputs

The JieLi AC79 SDK files required by `tools/fm1pkg_make.py`, the compiler,
container image and matching sound library are external dependencies.
Their required hashes and licence notes are in `EXTERNAL-BUILD-INPUTS.json`.
Package the pinned app and loader with product `FM-1_92200231`, key `0x980f`,
and the pinned SDK. No device commands are needed to compile or package.
The separately supplied `bb1-sounds-2.3.bin` is not embedded in this source
archive; it retains its own distribution terms.

## Source identity and licences

`SOURCE-BUNDLE-MANIFEST.json` lists every payload file except itself and
defines the exact tree-hash calculation. `BUILD-INPUTS.json` records the unchanged native source, compiler commands,
recipes and generated inputs from the reproduced application, loader and FWSC.
Release documentation and metadata carry the current maintenance version.
Original firmware is GPL-3.0-only. See `LICENSE`, `LICENSING.md`,
`BIG-BEAT-LICENSING.md` and `LICENSES/` for required component notices.
