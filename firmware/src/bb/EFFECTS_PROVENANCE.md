# Big Beat effects source attribution

`bb_echo.c` is an original Big Beat implementation of a three-head tape-style effect. It uses high/low-pass coloration, a bounded soft recording curve, fractional reads, slewed time and periodic wow. Its 128 KiB tape holds about 743 ms at 44.1 kHz; the longest exposed head is limited to 700 ms. It is not a circuit-accurate RE-201 or RE-501 model.

Motion uses deterministic slow head-position and stereo-pan targets, eased with an eight-second time constant and a separately smoothed depth control. Heads stay ordered; feedback is not randomized. It takes inspiration from the documented AMB concept without copying AMB code.

`bb_spring.c` and `.h` are GPL-3.0-only adaptations of the spring in Felucca revision `ef1f561b6b105f20350f6b1eac0dc0789a357f00`, `firmware/src/fx.c`, copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. The ten stretched allpasses, source gain staging and loop-feedback/damping range are retained. The adaptation uses float state, interpolated loop reads and per-sample size slew, and omits the original pickup wobble. It does not claim fixed-point equivalence or physical-tank accuracy.

These are component-source notes. The native release's selected units and flags are defined by `build23.py` and its recipe; the presence of a source component does not mean it is enabled in that release.
