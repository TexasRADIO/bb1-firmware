# Big Beat licensing and attribution

This addendum supplements `LICENSING.md` for BB1 additions.

## Firmware

BB1 firmware changes are GPL-3.0-only, following Felucca. The native 808 voices, Dillo and Telegram synthesis are in `firmware/src/bb/`.

The 9W9 TR-909 voice port derives from `er99_engine.c`, `er99_circuit.h`, `er99_tom909.h` and `er99_perc909.h`. It is GPL-3.0; see `LICENSES/909-reference-LICENSE` and the source notices.

The Euclid/Bjorklund generator in `firmware/src/bb/bb_gen.c` retains its GPL-3.0-only adaptation credit to tensor-programming/fm1-t1, `torso_t1_algo.c` at `bea0fd38fbebe48758059828a7ba107c6248304c`.

## Sound library and musical data

The maker declares redistribution authorization for the supplied LinnDrum, DrumTraks and DMX samples and Big Beat pattern library. This statement does not independently verify historical authenticity or source-machine identity. The sound library is supplied separately and is not relicensed by the firmware licence.

Acid Tabs phrases are credited to **J. McConaghy / Acid-Tabs.com** (https://acid-tabs.com), licensed **CC BY-SA 4.0** (https://creativecommons.org/licenses/by-sa/4.0/). They have been renamed, revoiced and retimed for Dillo. CC BY-SA 4.0 material may be adapted into GPLv3 under Creative Commons' declared one-way compatibility; these adaptations are distributed under GPLv3.

The ACID bank also adapts Acid Voice famous-pattern charts (https://acidvoice.com), covered by a separately held project licence. Step-format adaptations include ties as slides, TB-3 accent carry and 303 gates, with Texas Dillo naming. Per-phrase source titles and URLs remain in the catalogue where supplied.

Picnic Filled and Ghost Town Fill are by Taylor Rushing; included glyphs are used under the project-owner licence. Other font and component notices are retained with their source and under `LICENSES/`.

## External dependencies

The JieLi AC79 SDK portions inside a firmware package are Apache-2.0; see `LICENSING.md`. The JieLi compiler toolchain is an external dependency and is not redistributed here.
