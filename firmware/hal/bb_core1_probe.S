/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from fm1-x0x's fm1_cpu1.S, after Melodee and JieLi AC79 startup.
 * Retains the separate interrupt/user stacks; no voltage or frequency change. */
    .section .text.bb_core1_entry,"ax",@progbits
    .balign 4
    .globl bb_c1_entry
bb_c1_entry:
    cli
    r1 = bb_c1_trace
    r2 = 0xC1000001
    [r1 + 0] = r2
    icfg = 0
    usp = _bb_c1_ustack_top
    sp = _bb_c1_sstack_top
    ssp = _bb_c1_sstack_top
    reti = bb_c1_worker
    rti

    .section .bss.bb_c1_stack,"aw",@nobits
    .balign 8
    .globl _bb_c1_ustack
_bb_c1_ustack:
    .space 4096
    .globl _bb_c1_ustack_top
_bb_c1_ustack_top:
    .space 256
    .globl _bb_c1_sstack_top
_bb_c1_sstack_top:
