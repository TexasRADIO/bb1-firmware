/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only completed startup record. Does not invoke the diagnostic. */
bb_diag_counter("voicecost_version",bb_voice_cost.version);
bb_diag_counter("voicecost_status",bb_voice_cost.status);
bb_diag_counter("voicecost_cases",bb_voice_cost.cases);
bb_diag_counter("voicecost_failed_case",bb_voice_cost.failed_case);
bb_diag_counter("voicecost_elapsed",bb_voice_cost.elapsed);
bb_diag_counter("voicecost_calibration",bb_voice_cost.calibration);
for(unsigned sound=0;sound<D8S_NUM;sound++) {
    con_puts("voicecost_row ");con_hex(sound,2);
    for(unsigned size=0;size<4;size++) {
        con_putc(' ');con_hex(bb_voice_cost.ticks[sound][size],8);
    }
    con_puts("\r\n");
}
con_puts("voicecost_end 1\r\n");
