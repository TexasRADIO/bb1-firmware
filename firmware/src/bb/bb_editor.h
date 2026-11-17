/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_EDITOR_H
#define BB_EDITOR_H

#include <stdint.h>
#include "bb_editor_protocol.h"
#include "bb_control.h"

typedef int (*bb_editor_send_fn)(void *context, const uint8_t *sysex,
                                 unsigned length);

typedef struct {
    uint8_t connected, watching;
    uint16_t seq_counter;
    uint32_t revision;

    uint8_t tx_open, tx_domain;
    uint16_t tx_id;
    uint32_t tx_base_revision;
    bb_command staged;
    uint8_t staged_valid;

    /* Largest current wire message is 131 bytes (112-byte chunk + framing). */
    uint8_t reply_buf[136];
    uint16_t reply_len;
    uint8_t reply_pending;
    /* The BB1 v0 snapshot encodes to 125 bytes. */
    uint8_t snapshot_buf[128];
    uint16_t snapshot_len, snapshot_offset, snapshot_seq;
    uint8_t snapshot_index, snapshot_total, snapshot_active;
    uint32_t snapshot_crc;

    bb_commands *commands;
    bb_core *core;
    void *ui_state;
    bb_editor_send_fn send;
    void *send_context;
} bb_editor_t;

/* The adapter is main-loop only. The send callback must be non-blocking. */
int bb_editor_init(bb_editor_t *editor, bb_commands *commands, bb_core *core,
                   void *ui_state, bb_editor_send_fn send,
                   void *send_context);

/* `data` begins at message type and excludes the F0..family prefix and F7. */
void bb_editor_rx(bb_editor_t *editor, const uint8_t *data, unsigned length);
void bb_editor_tick(bb_editor_t *editor);

uint32_t bb_editor_schema_hash(void);

#endif
