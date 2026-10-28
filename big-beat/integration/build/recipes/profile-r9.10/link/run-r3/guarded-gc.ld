/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Retained linker layout plus a bounded supplementary crash record. */
/* Big Beat r9.7 (Claude Code, 2026-10-28): the run-r3 script with one change: .rodata is no longer KEEP,
 * so with -fdata-sections the linker drops read-only tables nothing references (3.8 KB of named tables
 * owned by functions already garbage-collected, plus their strings). Code is unchanged instruction for
 * instruction; see claude-code/integration/AUDIT-R9.7.md. Original: inputs.json guarded-gc.ld pin. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 72K
    POOL   (rw)  : ORIGIN = 0x01C1A000, LENGTH = 0x5A000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) KEEP(*(.c1_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .data : { . = ALIGN(4); _data_start = .; KEEP(*(.data*)) KEEP(*(.sdata*)) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; KEEP(*(.bss*)) KEEP(*(.sbss*)) KEEP(*(COMMON)) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; KEEP(*(.pool*)) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { KEEP(*(.noinit*)) } > NOINIT
    .crash_context (NOLOAD) : { KEEP(*(.crash_context*)) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
