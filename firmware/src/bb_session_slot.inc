/* SPDX-License-Identifier: GPL-3.0-only
 * Scene slot classification (moved from bb_target_session.c, save-fix r1).
 * Needs storage.c (st_current, st_head, st_read, st_sector, st_buf) and
 * bb_target_scene_decode(); bb/bb_save_claim.h names the classes.
 * Main loop only. Reads flash, never writes it. Included by
 * bb_target_session.c and by the host test (claude-code/save-fix/tests). */

/* one copy's commit header: 0 erased, 1 a FELU record (valid or damaged), 2 other data, -1 read error */
static int bb_session_copy_kind(unsigned obj, unsigned copy)
{
    uint8_t header[32];
    uint32_t magic;
    unsigned i, erased = 1;
    if (st_read(st_sector(obj, copy), header, sizeof header))
        return -1;
    for (i = 0; i < sizeof header; i++)
        if (header[i] != 255u) erased = 0;
    if (erased)
        return 0;
    magic = (uint32_t)header[0] | (uint32_t)header[1] << 8 | (uint32_t)header[2] << 16 | (uint32_t)header[3] << 24;
    return magic == ST_MAGIC ? 1 : 2;
}

/* BB_SLOT_BIG_BEAT, BB_SLOT_BLANK, BB_SLOT_PROTECTED or BB_SLOT_OTHER.
 * A committed copy must still decode as a scene (else PROTECTED, as 1.1).
 * With no committed copy: both headers erased is blank (a torn first save
 * with an erased commit header is retryable); a FELU header in either copy
 * (a damaged Big Beat or Felucca record) stays PROTECTED, as 1.1; only when
 * neither copy carries the FELU magic is it OTHER data, which SAVE may
 * replace after the held confirm and LOAD always refuses. */
static int bb_session_slot_state(unsigned obj)
{
    st_hdr_t h;
    int current = st_current(obj, &h), other = 0;
    if (current >= 0)
        return bb_target_scene_decode(st_buf, h.len) ? BB_SLOT_BIG_BEAT : BB_SLOT_PROTECTED;
    for (unsigned copy = 0; copy < 2; copy++) {
        int kind = bb_session_copy_kind(obj, copy);
        if (kind < 0 || kind == 1)
            return BB_SLOT_PROTECTED;
        if (kind == 2)
            other = 1;
    }
    return other ? BB_SLOT_OTHER : BB_SLOT_BLANK;
}
