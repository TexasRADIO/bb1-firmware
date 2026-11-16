/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_editor.h"
#include "bb_editor_snapshot.h"
#include "bb_fx_settings.h"
#include <string.h>

static uint32_t crc_byte(uint32_t crc, uint8_t value) {
    unsigned i;
    crc ^= value;
    for (i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    return crc;
}
static uint32_t crc_data(const uint8_t *data, unsigned length) {
    uint32_t crc = 0xFFFFFFFFu;
    while (length--) crc = crc_byte(crc, *data++);
    return crc ^ 0xFFFFFFFFu;
}
static uint32_t crc_u16(uint32_t crc, uint16_t value) {
    crc = crc_byte(crc, (uint8_t)(value >> 8));
    return crc_byte(crc, (uint8_t)value);
}

uint32_t bb_editor_schema_hash(void) {
    static const uint16_t fields[] = {
        BB_ED_FIELD_BPM, BB_ED_FIELD_SWING_X2, BB_ED_FIELD_RUNNING,
        BB_ED_FIELD_PAGE, BB_ED_FIELD_MACHINE, BB_ED_FIELD_VOICE,
        BB_ED_FIELD_DRUM_MUTED, BB_ED_FIELD_DRUM_LEVEL,
        BB_ED_FIELD_DRUM_LENGTH, BB_ED_FIELD_DRUM_B_MUTED,
        BB_ED_FIELD_DRUM_B_LEVEL, BB_ED_FIELD_DRUM_B_LENGTH,
        BB_ED_FIELD_DILLO_CUTOFF,
        BB_ED_FIELD_DILLO_RESONANCE, BB_ED_FIELD_DILLO_WAVE,
        BB_ED_FIELD_DILLO_ENVELOPE, BB_ED_FIELD_DILLO_DECAY,
        BB_ED_FIELD_DILLO_ACCENT, BB_ED_FIELD_DILLO_DRIVE,
        BB_ED_FIELD_DILLO_SLIDE_MS, BB_ED_FIELD_DILLO_ATTACK_MS,
        BB_ED_FIELD_DILLO_TRACKING, BB_ED_FIELD_FX_BASE,
        BB_ED_FIELD_FX_DELAY_TYPE, BB_ED_FIELD_FX_DELAY_DIV,
        BB_ED_FIELD_FX_DIST_TYPE, BB_ED_FIELD_FX_COMP_AMOUNT
    };
    uint32_t crc = 0xFFFFFFFFu;
    unsigned i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        crc = crc_u16(crc, fields[i]);
    crc = crc_byte(crc, BB_ED_PROTOCOL_VERSION);
    crc = crc_byte(crc, BB_ED_PRODUCT_BB1);
    return crc ^ 0xFFFFFFFFu;
}

static uint16_t read14(const uint8_t *p) {
    return (uint16_t)(((uint16_t)(p[0] & 0x7Fu) << 7) | (p[1] & 0x7Fu));
}
static uint32_t read28(const uint8_t *p) {
    return ((uint32_t)(p[0] & 0x7Fu) << 21) |
           ((uint32_t)(p[1] & 0x7Fu) << 14) |
           ((uint32_t)(p[2] & 0x7Fu) << 7) | (p[3] & 0x7Fu);
}

static void reply_begin(bb_editor_t *ed, uint8_t type, uint16_t seq) {
    ed->reply_buf[0] = 0xF0u;
    ed->reply_buf[1] = BB_ED_MFR_0;
    ed->reply_buf[2] = BB_ED_MFR_1;
    ed->reply_buf[3] = BB_ED_MFR_2;
    ed->reply_buf[4] = BB_ED_DEVICE;
    ed->reply_buf[5] = BB_ED_FAMILY;
    ed->reply_buf[6] = type;
    ed->reply_buf[7] = (uint8_t)((seq >> 7) & 0x7Fu);
    ed->reply_buf[8] = (uint8_t)(seq & 0x7Fu);
    ed->reply_len = 9;
}
static int reply_byte(bb_editor_t *ed, uint8_t value) {
    if (ed->reply_len >= sizeof(ed->reply_buf) - 1u || value > 0x7Fu) return 0;
    ed->reply_buf[ed->reply_len++] = value;
    return 1;
}
static void reply14(bb_editor_t *ed, uint16_t value) {
    reply_byte(ed, (uint8_t)((value >> 7) & 0x7Fu));
    reply_byte(ed, (uint8_t)(value & 0x7Fu));
}
static void reply32(bb_editor_t *ed, uint32_t value) {
    reply_byte(ed, (uint8_t)((value >> 28) & 0x0Fu));
    reply_byte(ed, (uint8_t)((value >> 21) & 0x7Fu));
    reply_byte(ed, (uint8_t)((value >> 14) & 0x7Fu));
    reply_byte(ed, (uint8_t)((value >> 7) & 0x7Fu));
    reply_byte(ed, (uint8_t)(value & 0x7Fu));
}
static int flush_reply(bb_editor_t *ed) {
    if (!ed->reply_pending || !ed->send) return 0;
    if (!ed->send(ed->send_context, ed->reply_buf, ed->reply_len)) return 0;
    ed->reply_pending = 0;
    ed->reply_len = 0;
    return 1;
}
static void reply_end(bb_editor_t *ed) {
    if (ed->reply_len < sizeof(ed->reply_buf))
        ed->reply_buf[ed->reply_len++] = 0xF7u;
    ed->reply_pending = 1;
    (void)flush_reply(ed);
}
static void result_reply(bb_editor_t *ed, uint8_t type, uint16_t seq,
                         uint8_t result) {
    reply_begin(ed, type, seq);
    reply_byte(ed, result);
    reply_end(ed);
}

static uint32_t current_revision(const bb_editor_t *ed) {
#if BB_WITH_PROJECT_FILES
    return ed->commands->project_edits ? ed->commands->project_edits : 1u;
#else
    return ed->revision ? ed->revision : 1u;
#endif
}

int bb_editor_init(bb_editor_t *ed, bb_commands *commands, bb_core *core,
                   void *ui_state, bb_editor_send_fn send, void *send_context) {
    if (!ed || !commands || !core || !send) return 0;
    memset(ed, 0, sizeof(*ed));
    ed->commands = commands;
    ed->core = core;
    ed->ui_state = ui_state;
    ed->send = send;
    ed->send_context = send_context;
    ed->revision = current_revision(ed);
    return 1;
}

static int map_field(uint8_t scope, uint8_t target, uint16_t field,
                     uint16_t value, bb_command *command) {
    unsigned machine;
    memset(command, 0, sizeof(*command));
    if (scope == BB_ED_SCOPE_GLOBAL) {
        if (field == BB_ED_FIELD_BPM && value >= 300u && value <= 3000u &&
            value % 10u == 0u) {
            command->type = BB_CMD_TEMPO;
            command->value = (uint16_t)(value / 10u);
            return 1;
        }
        if (field == BB_ED_FIELD_SWING_X2 && value >= 100u && value <= 150u) {
            command->type = BB_CMD_SWING_X2;
            command->value = value;
            return 1;
        }
        return 0;
    }
    if (scope == BB_ED_SCOPE_DRUM_A || scope == BB_ED_SCOPE_DRUM_B) {
        machine = scope == BB_ED_SCOPE_DRUM_A ? 0u : 1u;
        command->target = (uint8_t)machine;
        if (field == (machine ? BB_ED_FIELD_DRUM_B_MUTED : BB_ED_FIELD_DRUM_MUTED) && value <= 1u)
            command->type = BB_CMD_DRUM_MUTE;
        else if (field == (machine ? BB_ED_FIELD_DRUM_B_LEVEL : BB_ED_FIELD_DRUM_LEVEL) && value <= 127u)
            command->type = BB_CMD_DRUM_LEVEL;
        else if (field == (machine ? BB_ED_FIELD_DRUM_B_LENGTH : BB_ED_FIELD_DRUM_LENGTH) && value >= 1u && value <= BB_STEPS)
            command->type = BB_CMD_DRUM_LENGTH;
        else return 0;
        return 1;
    }
    if (scope == BB_ED_SCOPE_DILLO && field >= BB_ED_FIELD_DILLO_CUTOFF &&
        field <= BB_ED_FIELD_DILLO_TRACKING) {
        unsigned parameter = field - BB_ED_FIELD_DILLO_CUTOFF;
        unsigned minimum, maximum;
        if (!bb_synth_parameter_limits(0, parameter, &minimum, &maximum) ||
            value < minimum || value > maximum) return 0;
        command->type = BB_CMD_SYNTH_PARAMETER;
        command->target = 0;
        command->voice = (uint8_t)parameter;
        command->value = value;
        return 1;
    }
    if (scope == BB_ED_SCOPE_FX && field >= BB_ED_FIELD_FX_BASE &&
        field < BB_ED_FIELD_FX_BASE + BB_FX_PARAMETER_COUNT) {
        unsigned parameter = field - BB_ED_FIELD_FX_BASE;
        unsigned minimum, maximum;
        if (!bb_fx_parameter_limits(parameter, &minimum, &maximum) ||
            value < minimum || value > maximum) return 0;
        command->type = BB_CMD_FX_PARAMETER;
        command->voice = (uint8_t)parameter;
        command->value = value;
        return 1;
    }
    if (scope == BB_ED_SCOPE_MIX && target < BB_MIX_CHANNELS &&
        field == (uint16_t)(0x0500u + target) && value <= 127u) {
        command->type = BB_CMD_BAND_LEVEL;
        command->target = target;
        command->value = value;
        return 1;
    }
    return 0;
}

static void handle_hello(bb_editor_t *ed, const uint8_t *data, unsigned length,
                         uint16_t seq) {
    uint8_t protocol = length ? data[0] : 0x7Fu;
    if (protocol != BB_ED_PROTOCOL_VERSION) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_NOT_SUPPORTED);
        return;
    }
    reply_begin(ed, BB_ED_MSG_HELLO_REPLY, seq);
    reply_byte(ed, BB_ED_PROTOCOL_VERSION);
    reply_byte(ed, BB_ED_PRODUCT_BB1);
    reply14(ed, 23u);
    reply32(ed, bb_editor_schema_hash());
    reply_byte(ed, BB_ED_MAX_CHUNK_RAW);
    /* Snapshot, transport and live patch editing. WATCH remains accepted for
     * forward compatibility but is not advertised until DELTA exists. */
    reply_byte(ed, 0x45u);
    reply14(ed, 44u);
    reply14(ed, 0u);
    reply14(ed, 0u);
    reply_end(ed);
    ed->connected = 1;
}

static unsigned pack7(const uint8_t *input, unsigned length, uint8_t *output,
                      unsigned capacity) {
    unsigned i = 0, out = 0;
    while (i < length) {
        unsigned j, count = length - i > 7u ? 7u : length - i;
        uint8_t msb = 0;
        if (out + count + 1u > capacity) return 0;
        for (j = 0; j < count; ++j)
            if (input[i + j] & 0x80u) msb |= (uint8_t)(1u << j);
        output[out++] = msb;
        for (j = 0; j < count; ++j) output[out++] = input[i + j] & 0x7Fu;
        i += count;
    }
    return out;
}

static void queue_snapshot_part(bb_editor_t *ed) {
    unsigned remaining, count, i;
    if (!ed->snapshot_active || ed->reply_pending) return;
    if (ed->snapshot_index >= ed->snapshot_total) {
        reply_begin(ed, BB_ED_MSG_SNAPSHOT_END, ed->snapshot_seq);
        reply_end(ed);
        ed->snapshot_active = 0;
        return;
    }
    remaining = ed->snapshot_len - ed->snapshot_offset;
    count = remaining > BB_ED_MAX_CHUNK_RAW ? BB_ED_MAX_CHUNK_RAW : remaining;
    reply_begin(ed, BB_ED_MSG_SNAPSHOT_CHUNK, ed->snapshot_seq);
    reply14(ed, ed->snapshot_index);
    reply14(ed, ed->snapshot_total);
    for (i = 0; i < count; ++i)
        reply_byte(ed, ed->snapshot_buf[ed->snapshot_offset + i]);
    ed->snapshot_offset = (uint16_t)(ed->snapshot_offset + count);
    if (ed->snapshot_index + 1u == ed->snapshot_total)
        reply32(ed, ed->snapshot_crc);
    ed->snapshot_index++;
    reply_end(ed);
}

static void handle_snapshot(bb_editor_t *ed, uint16_t seq) {
    bb_editor_snapshot snapshot;
    unsigned length;
    if (ed->reply_pending || ed->snapshot_active || !bb_commands_idle(ed->commands)) {
        result_reply(ed, BB_ED_MSG_BUSY, seq, BB_ED_RESULT_BUSY);
        return;
    }
    ed->revision = current_revision(ed);
    if (!bb_editor_snapshot_build(&snapshot, ed->core, ed->revision)) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_REFUSED);
        return;
    }
    length = pack7(snapshot.buf, snapshot.len, ed->snapshot_buf,
                   sizeof(ed->snapshot_buf));
    if (!length) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_CAPACITY_EXCEEDED);
        return;
    }
    ed->snapshot_len = (uint16_t)length;
    ed->snapshot_offset = 0;
    ed->snapshot_seq = seq;
    ed->snapshot_index = 0;
    ed->snapshot_total = (uint8_t)((length + BB_ED_MAX_CHUNK_RAW - 1u) /
                                   BB_ED_MAX_CHUNK_RAW);
    ed->snapshot_crc = crc_data(snapshot.buf, snapshot.len);
    ed->snapshot_active = 1;
    queue_snapshot_part(ed);
}

static void handle_tx_begin(bb_editor_t *ed, const uint8_t *payload,
                            unsigned length, uint16_t seq) {
    uint32_t base;
    if (!ed->connected || length < 7u || ed->tx_open) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_REFUSED);
        return;
    }
    base = read28(payload);
    ed->revision = current_revision(ed);
    if (base && base != ed->revision) {
        reply_begin(ed, BB_ED_MSG_CONFLICT, seq);
        reply32(ed, ed->revision);
        reply_end(ed);
        return;
    }
    ed->tx_domain = payload[4] & 0x7Fu;
    ed->tx_id = read14(payload + 5);
    ed->tx_base_revision = base;
    ed->tx_open = 1;
    ed->staged_valid = 0;
    result_reply(ed, BB_ED_MSG_ACK, seq, BB_ED_RESULT_OK);
}

static void handle_set(bb_editor_t *ed, const uint8_t *payload,
                       unsigned length, uint16_t seq) {
    bb_command command;
    if (!ed->tx_open || ed->staged_valid || length < 6u ||
        payload[0] != ed->tx_domain) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_REFUSED);
        return;
    }
    if (!map_field(payload[0], payload[1], read14(payload + 2),
                   read14(payload + 4), &command)) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_UNKNOWN_FIELD);
        return;
    }
    ed->staged = command;
    ed->staged_valid = 1;
    result_reply(ed, BB_ED_MSG_APPLIED, seq, BB_ED_RESULT_OK);
}

static void close_tx(bb_editor_t *ed) {
    ed->tx_open = 0;
    ed->staged_valid = 0;
    ed->tx_id = 0;
    ed->tx_domain = 0;
}

static void handle_commit(bb_editor_t *ed, const uint8_t *payload,
                          unsigned length, uint16_t seq) {
    if (!ed->tx_open || !ed->staged_valid || length < 2u ||
        read14(payload) != ed->tx_id) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_REFUSED);
        return;
    }
    if (!bb_command_push(ed->commands, &ed->staged)) {
        result_reply(ed, BB_ED_MSG_BUSY, seq, BB_ED_RESULT_BUSY);
        return;
    }
#if !BB_WITH_PROJECT_FILES
    ed->revision++;
#endif
    close_tx(ed);
    result_reply(ed, BB_ED_MSG_ACK, seq, BB_ED_RESULT_QUEUED);
}

static void handle_abort(bb_editor_t *ed, const uint8_t *payload,
                         unsigned length, uint16_t seq) {
    if (!ed->tx_open || length < 2u || read14(payload) != ed->tx_id) {
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_REFUSED);
        return;
    }
    close_tx(ed);
    result_reply(ed, BB_ED_MSG_ACK, seq, BB_ED_RESULT_OK);
}

static void handle_transport(bb_editor_t *ed, uint8_t type, uint16_t seq) {
    bb_command command;
    memset(&command, 0, sizeof(command));
    command.type = BB_CMD_TRANSPORT;
    command.value = type == BB_ED_MSG_PLAY ? 1u : 0u;
    if (!bb_command_push(ed->commands, &command))
        result_reply(ed, BB_ED_MSG_BUSY, seq, BB_ED_RESULT_BUSY);
    else
        result_reply(ed, BB_ED_MSG_ACK, seq, BB_ED_RESULT_QUEUED);
}

void bb_editor_rx(bb_editor_t *ed, const uint8_t *data, unsigned length) {
    uint8_t type;
    uint16_t seq;
    const uint8_t *payload;
    unsigned payload_length;
    if (!ed || !data || length < 3u || ed->reply_pending) return;
    type = data[0]; seq = read14(data + 1);
    payload = data + 3; payload_length = length - 3u;
    switch (type) {
    case BB_ED_MSG_HELLO: handle_hello(ed, payload, payload_length, seq); break;
    case BB_ED_MSG_SNAPSHOT_BEGIN: handle_snapshot(ed, seq); break;
    case BB_ED_MSG_WATCH:
        ed->watching = payload_length && payload[0] ? 1u : 0u;
        result_reply(ed, BB_ED_MSG_ACK, seq, BB_ED_RESULT_OK); break;
    case BB_ED_MSG_TX_BEGIN: handle_tx_begin(ed, payload, payload_length, seq); break;
    case BB_ED_MSG_SET: handle_set(ed, payload, payload_length, seq); break;
    case BB_ED_MSG_TX_COMMIT: handle_commit(ed, payload, payload_length, seq); break;
    case BB_ED_MSG_TX_ABORT: handle_abort(ed, payload, payload_length, seq); break;
    case BB_ED_MSG_PLAY: case BB_ED_MSG_STOP: handle_transport(ed, type, seq); break;
    case BB_ED_MSG_PING:
        reply_begin(ed, BB_ED_MSG_PONG, seq); reply_end(ed); break;
    case BB_ED_MSG_SETTLE:
        reply_begin(ed, BB_ED_MSG_SETTLE_REPLY, seq);
        reply32(ed, current_revision(ed));
        reply_byte(ed, ed->tx_open);
        reply_end(ed); break;
    default:
        result_reply(ed, BB_ED_MSG_REJECT, seq, BB_ED_RESULT_NOT_SUPPORTED); break;
    }
}

void bb_editor_tick(bb_editor_t *ed) {
    if (!ed) return;
    if (ed->reply_pending && !flush_reply(ed)) return;
    queue_snapshot_part(ed);
}
