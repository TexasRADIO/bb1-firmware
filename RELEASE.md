# BB1 2.3 shared editor

BB1 combines two independent drum layers with Dillo bass, effects, per-song effects recall, retrospective capture, Screen companion and Sideman USB transmission.

This package adds the shared-editor working-state protocol to BB1 2.3. It supports reading current working state and sending edits. Editor state files are distinct from full device projects; native project transfer and automatic device-originated parameter updates are outside this package's documented editor surface.

## Files

- `bb1-2.3-editor.fwsc`: FM-1 application firmware, product `FM-1_92200203`.
- `bb1-sounds-2.3.bin`: matching BB1 sound library.
- `bb1-source-2.3-editor.tar.gz`: corresponding source, build instructions and licence notices.
- `fmhub.json`: Firmware Hub product card.
- `SHA256SUMS`: download checksums.

Use the [BB1 installer](https://thebigbeat.app/fm1/install/) and [player's guide](https://thebigbeat.app/fm1/guide/). The firmware and separately supplied sound library retain the terms described in the source tree. This release is for the FM-1; no SMK-37 compatibility is claimed.
