/* SPDX-License-Identifier: GPL-3.0-only
 * Known 1 MiB SPI NOR parts fitted to the M-VAVE FM-1.
 *
 * Both parts use the command geometry already implemented by fm1_flash.h:
 * JEDEC ID 0x9F, status 0x05/0x35, write enable 0x06, 4 KiB erase 0x20,
 * page program 0x02 and 24-bit addressing.  An unlisted ID remains read-only
 * at the policy level: the application keeps saves in RAM and recovery refuses
 * an update rather than guessing from the capacity byte alone.
 *
 * 0x856014  Puya P25Q80 used by the original FM-1
 * 0xB36014  UC25HQ80 observed in later FM-1 production
 */
#pragma once

#define FL_JEDEC_OK(id) \
    ((uint32_t)(id) == 0x856014u || (uint32_t)(id) == 0xB36014u)
