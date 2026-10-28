/* SPDX-License-Identifier: GPL-3.0-only
 * Minimal CDC service for the USB recovery loop. Never dispatch the normal
 * instrument console before the instrument has been initialized. Reuse its
 * line buffer because the normal and recovery loops cannot run together.
 */
static uint32_t con_recovery_retry_deadline;

static int con_recovery_exec(const char *line)
{
    if (str_eq(line, "recovery status")) {
        con_kv("recovery_version", 1);
        con_kv("safe_mode", 1);
        con_kv("audio_disabled", 1);
        con_kv("failed_starts", (int32_t)bootguard.failed);
        con_kv("boot_pending", (int32_t)bootguard.pending);
        con_kv("flash", flash_ok);
    } else if (str_eq(line, "recovery crash")) {
        con_crash();
    } else if (str_eq(line, "recovery retry normal")) {
        con_puts("retry_normal accepted\r\n");
        return 1;
    } else if (*line) {
        con_puts("recovery status | recovery crash | recovery retry normal\r\n");
    }
    return 0;
}

/* At most 32 received bytes and one complete command per call. A long line
 * is discarded through its terminator, so truncation cannot accept a command.
 * A retry is returned only after the reply drains, or 100 ms elapses. DTR
 * loss cancels it. The caller alone owns bootguard changes and the restart.
 */
static int cdc_recovery_task(void)
{
    unsigned budget = 32;
    if (!cdc.dtr) {
        con.len = 0;
        con.dtr_seen = 0;
        con_recovery_retry_deadline = 0;
        ci_r = ci_w;
        return 0;
    }
    if (!con.dtr_seen) {
        con.len = 0;
        con.stalled = 0;
        con.dtr_seen = 1;
        con_puts("\r\nBig Beat USB recovery; type recovery status\r\n> ");
    }
    if (con_recovery_retry_deadline) {
        if (co_r == co_w ||
            (int32_t)(fm1_ms - con_recovery_retry_deadline) >= 0) {
            con_recovery_retry_deadline = 0;
            return 1;
        }
        return 0;
    }
    while (budget-- && ci_r != ci_w) {
        char c;
        RING_PUBLISH();
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            int retry = 0;
            if (!con.len)
                continue;
            con.stalled = 0;
            if (con.len < CON_LINE) {
                con.line[con.len] = 0;
                retry = con_recovery_exec(con.line);
            } else {
                con_puts("recovery command rejected\r\n");
            }
            con.len = 0;
            con_puts("> ");
            if (retry) {
                con_recovery_retry_deadline = fm1_ms + 100u;
                if (!con_recovery_retry_deadline)
                    con_recovery_retry_deadline = 1u;
            }
            break;
        } else if ((c == 0x7f || c == 0x08) && con.len < CON_LINE) {
            if (con.len)
                con.len--;
        } else if (c >= ' ' && c < 0x7f && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
        } else {
            /* Non-ASCII input and overflowing lines remain invalid until EOL. */
            con.len = CON_LINE;
        }
    }
    return 0;
}
