# Big Beat Studio BB1 2.0

Big Beat Studio BB1 is free rhythm-machine firmware for the M-VAVE FM-1.

## FOLKS! HOW MANY DRUM MACHINES CAN YOU FIT IN ONE LITTLE BOX?

**WITH BIG BEAT STUDIO BB1, YOU CAN PLAY TWO AT ONCE!**

Put an **808** on one side and a **LinnDrum** on the other! Bring in the **DMX! DrumTraks! Linn LM-1! Simmons! Rhythm King! Drumulator!**

Choose your machines. Give each one its own pattern. Stack them up and LET ’EM GO!

**NEED A RHYTHM? WE’VE GOT 539 SECTIONS READY TO PLAY!**

That’s **67 songs**, **21 musical eras**, and **138 classic rhythms from four vintage Roland rhythm boxes** — all in the catalogue.

**AND THERE’S A BASS SYNTH IN THERE TOO!**

Dillo brings **205 ACID phrases across 27 banks**, with accents, slides and hands-on sound shaping. Open that filter! Give it some bite! Send the whole show through **tape echo and spring reverb!**

## BUT WAIT — THERE’S MORE!

BIG BEAT STUDIO BB1 2.0 now brings you:

- **31 MORE DRUM KITS!**
- **A SONG ARRANGER WITH NAMED SECTIONS!**
- **BUILD SCREENS FOR MAKING YOUR OWN KITS!**
- **RATCHETS! REVERSIBLE EVOLVE! SWUNG RECORDING! EDITING AND UNDO!**

You get the rhythm machine — AND the new features!

**ALL FOR $19.99? NO! IT’S FREE!**

No instalments. No subscription. No shipping and handling.

**BIG BEAT STUDIO BB1 2.0. TWO BEATS. ONE LITTLE BOX.**

*For the M-VAVE FM-1. Hardware required.*

---

## Downloads and starting firmware

Use `bb1-2.0.fwsc` together with the matching `bb1-sounds-1.4.bin`. The source archive and browser corresponding-source ZIP are included. SHA256SUMS pins all four downloads.

The prepared BB1 installer admits corrected BB1 1.5 (`FM-1_92200105`) or this exact 2.0 identity (`FM-1_92200200`). Do not assume a different firmware family or an old `FM-1_900` package is interchangeable. Back up material before switching firmware; scene compatibility and byte preservation are separate questions. Earlier releases remain available for rollback.

## Known issue and verification scope

This release is published at the owner's direction with one known native audio deadline overrun deferred to a minor follow-up: a callback took 6,031.5 microseconds against a 5,802.667 microsecond deadline, recording one deadline exceed and one DMA miss. Later separate bounded runs were clean; they do not explain the original event or prove uninterrupted playback under every workload. A glitch remains possible.

The native build and package are independently reproduced. The corrected SONG/audio/navigation checks and the website's 969-test suite pass at their documented scopes. App-only installation and return to corrected 1.5 passed with two byte-identical readbacks of app, user and both sound regions. The matched-library return is now proven: original1.2 sounds and corrected1.5 firmware were restored, followed by2.0 and library1.4; two full readbacks at each endpoint matched the fresh saved-data reference and unchanged regionB. Formal whole-stack acceptance and exhaustive physical/persistence qualification are not represented as completed passes. These limitations remain disclosed; publication does not convert them into test results.

The measurement/profiling firmware is not part of this release. The playable 2.0 emulator already exists; browser results are separate from physical FM-1 timing. Licence and source notices remain included; no proprietary toolchain is redistributed.
