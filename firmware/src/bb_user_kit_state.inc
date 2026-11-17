/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.0 kit builder: firmware state and the scene/project side (paired scenes only).
 * Included by dual-sample-platform-r1.inc, so the paired scene codec can carry drum A's USER kit.
 * Main loop only, except where a function says it runs inside a paused session/project transaction.
 *
 * Memory: .pool only, never .bss (ordinary RAM is at its 4,096 B floor), about 1.2 KB. USER 1-4 stay in flash (the
 * BBUB object); only their names are kept here. Drum A plays ONE table (res); a new table is resolved into the
 * session scratch (bb_user_kit_firmware.inc) and copied under the command lock, so no second table is kept. */
#if BB_WITH_USER_KITS
#if !defined(BB_SAMPLE_DUAL_TRIAL) || !BB_WITH_RICH_DRUM_RENDER
#error "The kit builder needs the paired sample scenes and the rich drum renderer"
#endif
#define BB_UK_SMALL __attribute__((minsize))   /* main loop only, never the sound: built for size */
#define BB_UK_ORIGIN_DRAFT 7u              /* scene kit byte of a USER layer that is an unsaved BUILD draft */
typedef struct {
    bb_user_kit draft;                     /* BUILD's kit; while drum A plays USER, the copy scenes carry */
    bb_user_kit kit[2];                    /* [0] a USER kit being loaded, [1] the draft before an edit; a scene
                                            * decode (paused, nothing loading or editing) stages its copies here */
    bb_uk_resolved res;                    /* the table drum A plays (bb_audio.user[0]) */
    bb_uk_machine m[9];                    /* installed machines: the 808, then the pack's sampled kits */
    char name[BB_UK_SLOTS][BB_UK_NAME], text[24];
    uint32_t sub, miss, ticket;
    uint8_t nm, used, foreign, lane, origin, live, loading, load_slot, save_slot, scene_mask, stored, edits;
} bb_uk_state;
static bb_uk_state bb_uk __attribute__((section(".pool")));

/* Resolve kit into r against the installed machines and remember what the screen marks (~ and ?). */
static BB_UK_SMALL int bb_uk_resolve_into(const bb_user_kit *kit, bb_uk_resolved *r)
{
    int result = bb_uk_resolve(kit, bb_uk.m, bb_uk.nm, bb_audio_aux_capabilities(&bb_sound), r);
    if (result == BB_UK_OK) { bb_uk.sub = r->substituted; bb_uk.miss = r->missing; }
    return result;
}
/* Restore (owners paused), before any mutation: a scene whose drum A plays a USER kit resolves its copy into the
 * table now. -1 = refuse the scene, 0 = no USER layer, 1 = ready. */
static BB_UK_SMALL int bb_uk_scene_prepare(void)
{
    if (!(bb_uk.scene_mask & 1u)) return 0;
    if ((bb_uk.scene_mask & 2u) || !bb_uk.nm) return -1;   /* this build plays USER kits on drum A only */
    return bb_uk_resolve_into(&bb_uk.kit[0], &bb_uk.res) == BB_UK_OK ? 1 : -1;
}
/* Restore succeeded with drum A on the USER id: install the table (owners still paused). */
static BB_UK_SMALL int bb_uk_scene_installed(int user, unsigned kit_byte)
{
    if (user <= 0) return 1;
    if (!bb_audio_user_kit_swap_owned(&bb_sound, 0, &bb_uk.res)) return 0;
    bb_uk.draft = bb_uk.kit[0]; bb_uk.live = 1; bb_uk.loading = 0;
    bb_uk.origin = (uint8_t)(kit_byte < BB_UK_SLOTS ? kit_byte : BB_UK_ORIGIN_DRAFT);
    bb_uk.edits = 0;
    return 1;
}
#endif
