# BB1 2.3 — native Firmware Hub package

BB1 combines two drum layers, sampled machines, a synthesised 808 and Dillo bass,
with effects, browser editing, per-song effects recall, retrospective capture,
Screen companion and Sideman USB transmission. Target: M-VAVE FM-1 only.

This packaging variant retains the shared-editor 2.3 application,
`FM-1_92200203`. A single `bb1-2.3.fwsc` carries both that application and its
matching 240 KiB sound library. No second sound-library transfer is required.
The packaging release tag is `v2.3-hub.1`.

[Player’s guide](https://thebigbeat.app/fm1/guide/) ·
[Browser editor](https://thebigbeat.app/editor/) ·
[Browser edition](https://thebigbeat.app/fm1/emulator/)

## Build the application and loader

The pinned JieLi PI32v2 toolchain runs on x86-64 Linux. From this directory:

```sh
JIELI_TOOLCHAIN=/absolute/path/to/toolchain python3 build23.py
JIELI_TOOLCHAIN=/absolute/path/to/toolchain python3 -c 'import sys; sys.path.insert(0,"tools"); import build; build.build_loader()'
```

The application hash guard remains enabled. The expected application is
576876 bytes, SHA-256 `7f956afca2e77b918721896ea3ceed51a386a965ef5eed4a360d8d71fdeaa9de`.
The full-install RAM loader is 8364 bytes, SHA-256
`72eea6f6d3ff8f085f3f1fffa3827edc01c7d4a2698f4dc555bb23a41f76955b`.
Both were reproduced with the inputs in `EXTERNAL-BUILD-INPUTS.json`.

## Build the complete package

Fetch the exact baseline application container and matching library identified
in `EXTERNAL-BUILD-INPUTS.json`. Then run:

```sh
python3 tools/package_hub_install.py --base /path/to/bb1-2.3-editor.fwsc --sounds /path/to/bb1-sounds-2.3.bin --loader build/loader/loader.bin --output /path/to/bb1-2.3.fwsc --expect-product FM-1_92200203 --expect-base-sha256 7b52044b8cf381f529d95eb4f98aa313a50052a10d9868723b94b4fdc854f9b6 --expect-sounds-sha256 8c2c3edb14bcbf4d91ff8b80ccf172f659c7ea837875d88819b78f8fe8ccfe2a
python3 tools/verify_hub_install.py --package /path/to/bb1-2.3.fwsc --base /path/to/bb1-2.3-editor.fwsc --sounds /path/to/bb1-sounds-2.3.bin --loader build/loader/loader.bin --product FM-1_92200203
```

The builder preserves the baseline application flash verbatim, embeds the exact
library and inserts the reproduced full-install loader. Expected package:
1056701 bytes, SHA-256 `a272334fd67c16a825f30bb68684ed4dce95dcf571b9607d0530736b1561d23e`.
The application/content write windows are `0x4000..0x93000` and
`0xA0000..0xDC000`. The loader validates the complete image before erasing,
verifies written sectors and handles the temporary updater record separately.
It preserves the boot head and other application/user regions.

The shared editor exposes the current bounded working state. Editor state files
are distinct from full device projects; native project transfer and automatic
hardware-originated watch updates are separate capabilities.

## Source and licences

`SOURCE-BUNDLE-MANIFEST.json` inventories the corresponding source.
`RELEASE-BUILD-PINS.json` names the exact application, loader and package.
Original firmware is GPL-3.0-only. Component and library terms remain in
`LICENSE`, `LICENSING.md`, `BIG-BEAT-LICENSING.md` and `LICENSES/`.
