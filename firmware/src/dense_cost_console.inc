/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only startup result; never invokes synthesis or transport. */
bb_diag_counter("densecost_version",1u);
bb_diag_counter("densecost_passed",bb_dense_cost.passed);
bb_diag_counter("densecost_cases",bb_dense_cost.cases);
for(unsigned i=0;i<12;i++) {
    con_puts("densecost_row ");con_hex(i,2);con_putc(' ');
    con_hex(bb_dense_cost.ticks[i],8);con_puts("\r\n");
}
con_puts("densecost_end 1\r\n");
