```
                         \   |   /
                          \  |  /        ((( ON AIR )))
                     ------ (*) ------
                          /  |  \
                         /   |   \
                            /|\
                           / | \
               _.--""""""""""""""""""--._
           _.-'  |    |    |    |    |   '-._          /\
        .-'  |   |    |    |    |    |    |  '-.      /  \
       /  |  |   |    |    |    |    |    |   | \____/  o \
      ;   |  |   |    |    |    |    |    |   |  ____    __>
 ____/____|__|___|____|____|____|____|____|___|_/    '--'
'----'   /  /                              \  \
        (__(                               (__(
```

```
  ____   ____   _       TWO BEATS.  ONE LITTLE BOX.
 | __ ) | __ ) / |
 |  _ \ |  _ \ | |      firmware for the M-VAVE FM-1
 | |_) || |_) || |      two drum machines · a bass that bites
 |____/ |____/ |_|      tape echo · spring · no computer needed
```

# BB1 — the FM-1 becomes a rhythm machine

**Two drum machines at once. A bass synth with attitude. A tape echo and a spring on everything.** BB1 rewires the FM-1 into a pocket studio built for one job: grooves. Load a beat from five decades of drum machines, stack a second machine on top, let **Dillo** find the bassline, then grab the knobs and push it until something breaks.

```
          1 . . . 2 . . . 3 . . . 4 . . .
  KIT A   ■ · · · · · ■ · · · ■ · · · · ·   808, live
  KIT B   · · · · ■ · · · · · · · ■ · · ·   DMX, sampled
  DILLO   ■ · ■ ■ · · ■ · ■ · · ■ · · ■ ·   acid bass
  ECHO    ═══════════════════════════════▶  REC + FX
```

## What's in the box

|  |  |
|---|---|
| **KIT A + KIT B** | Two independent drum machines running together, each with its own pattern, kit and level. A Linn groove under an 808? Go on. |
| **Four machines** | The **808**, synthesised in real time, plus sampled **LinnDrum**, **DrumTraks** and **DMX**. Swap the machine under a pattern and it lands on the next bar. |
| **Dillo** | A monophonic bass synth with eight presets, from rubbery acid to hollow dub. Every era brings its own line. |
| **Echo & spring** | A three-head tape echo and a spring reverb, shared by everything. Hold **REC + FX** for a dub throw. |
| **67 songs** | Drum patterns in the style of records you know, section by section. |
| **21 eras** | Big Beat originals, from Motorik to rave, each with its own Dillo bassline. |
| **138 classic rhythms** | The preset rhythms of four vintage rhythm boxes: waltzes, bossa novas, rock and everything between. |
| **Changes on the beat** | New kits and patterns wait for the bar line. The screen says **NEXT** until they land. |

## Get it on your FM-1

1. Plug the FM-1 into a computer with a USB **data** cable, and open **[thebigbeat.app/fm1/install](https://thebigbeat.app/fm1/install/)** in Chrome or Edge.
2. Press **Install BB1**, then **Install the sounds**. About five minutes. Your projects, presets and settings stay where they are.
3. Turn **MASTER** up, press **HOME**, press **PLAY**.

Want it gone? The same page takes you back to the official firmware in one click, after saving a backup of your music.

**Try before you flash:** the [browser player](https://thebigbeat.app/fm1/emulator/) runs the real BB1 firmware, the same screen and the same engine, right in your browser. Then read **[the guide](https://thebigbeat.app/fm1/guide/)**.

## Controls in ten seconds

```
  HOME ········· back to STUDIO from anywhere (hold it for help)
  SELECT ······· who you're playing: KIT A · KIT B · DILLO
  PRESETS ······ turn to browse kits, EDIT to load
  ALGORITHM ···· turn to browse songs, eras and patterns, EDIT to load
  FX ··········· echo, tape, spring, motion, sends
  REC + FX ····· dub throw: the echo opens wide, then snaps back
  four knobs ··· always do what the bottom of the screen says
```

## Build it yourself

Everything here is the complete source of the released firmware. One command rebuilds it and proves it matches:

```sh
python3 -I big-beat/integration/verify_release.py . /tmp/bb1-check \
  --expect-app a5e34e31a854eb1f731b8633350f751e2454c775c4131448b761b8f0e39f178f \
  --expect-package 5345b617e7e8fc13460bfbdbc9d47019742002631ebb7aedd25f9639ef506718
```

Those are the identities of **BB1 1.0**, the build the web installer ships (`bb1-1.0.fwsc`, 610,086 bytes). The sound library is `f4bbf1a409d8aadb3429232e49dfb06b121070532787d31078db9630b6e7efcc` (245,760 bytes).

It runs the host test suites, the audio oracle, a 480-section level sweep, the from-source target build and the deterministic packaging, with negative cases. Exit 0 means the bytes match the release, bit for bit. The pinned toolchain and build inputs are listed in `big-beat/integration/inputs.json`.

## Licence

GNU GPL v3. See `LICENSE`, `LICENSING.md`, `LICENSES/` and `BIG-BEAT-LICENSING.md`. BB1 is a Felucca-family firmware; upstream notices are kept in `LICENSING.md`. M-VAVE, FM-1, Roland, TR-808, LinnDrum, DrumTraks, Sequential, DMX and Oberheim are trademarks of their respective owners. BB1 is independent and not affiliated with or endorsed by them. **No warranty:** you install it at your own risk.

```
  ────────────────────────────────────────────────────────────────
   OLE BIG BEAT LTD  ·  thebigbeat.app
  ────────────────────────────────────────────────────────────────
```
