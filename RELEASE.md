# BB1 2.3 native Firmware Hub package

BB1 combines two independent drum layers with Dillo bass, effects, per-song effects recall, retrospective capture, Screen companion and Sideman USB transmission.

This release keeps the BB1 2.3 application and shared-editor working-state protocol unchanged. It replaces the split application-and-library install with one self-contained Firmware Hub package. The native Hub installer now writes the application and its exact matching sound library in one operation.

## Files

- `bb1-2.3-editor.2.fwsc`: self-contained FM-1 application and matching sound library, product `FM-1_92200203`.
- `bb1-sounds-2.3.bin`: matching BB1 sound library.
- `bb1-source-2.3-editor.2.tar.gz`: corresponding source, build instructions and licence notices.
- `fmhub.json`: Firmware Hub product card.
- `SHA256SUMS`: download checksums.

The package was installed and booted on a physical M-VAVE FM-1. Its bounded loader writes only `0x4000..0x93000` and `0xA0000..0xDC000`, verifies every written sector, resumes safely after an interrupted transfer and refuses a damaged package before erasing. The firmware and sound library retain the terms described in the source tree. This release is for the FM-1; no SMK-37 compatibility is claimed.
