/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 Live Play P2: live Dillo patch audition (SPEC 3), the firmware adapter (included by bb_nav_firmware.inc; the
 * host test big-beat/integration/tests/dillo_keep.c compiles this same file). Main loop only.
 * Dillo PATCH page: the keys stay notes, PRESETS browses, and the sound loads once the cursor has rested on another
 * patch for BB_AUD_SETTLE_MS (Joe: 350 ms), one command per settle, never per detent. EDIT = KEEP loads now and stays.
 * The first detent saves a restore point (patch, its EDITED / scene marks, the eight sound parameters); HOME from the
 * page puts it back (SOUND RESTORED), any other exit keeps the new sound. A picker request still pending blocks the
 * audition (one owner of the sound); a refused push (queue full) is retried on the next scan, latest cursor wins.
 * RC1/RC2 (Duke #64 6592267384, 6599173185): KEEP is owed until the owner is OBSERVED playing the patch under the
 * cursor, on every path (an accepted settle the owner then drops included). A refused push or a request in flight
 * leaves it owed and it is retried on every scan; only then does the audition end and the screen say SOUND KEPT.
 * Leaving the page or HOME drops it (nothing was claimed).
 * The existing BB_CMD_DILLO_PATCH / BB_CMD_SYNTH_PARAMETER commands; state in the pool. */
#include "bb_dillo_audition.h"
static bb_audition bb_aud __attribute__((section(".pool")));
static int bb_aud_page(void){return bb_nav.page==BB_NAV_KIT&&!bb_nav_is_drum();}
static unsigned bb_aud_get(unsigned p){return bb_synth_parameter_get(&bb_face.requested,0,p);}
static int bb_aud_send(int patch,unsigned value,unsigned p){return bb_send_synth(0,patch?BB_CMD_DILLO_PATCH:BB_CMD_SYNTH_PARAMETER,value,p);}
/* the owner plays the patch under the cursor: that factory patch, or an edit of it (RC2: an edited sound reads as
 * active -1, so its patch is the label bb_patch_index, which the picker poll sets only from an applied sound) */
static int bb_aud_playing(void) {
    bb_sound_picker_view view;
    if(!bb_sound_picker_observe(&bb_sound_pick,&bb_sound,&view))return 0;
    return view.active_index==(int)bb_sound_pick.cursor||(view.active_index<0&&bb_patch_index[0]==bb_sound_pick.cursor);
}
/* Load the patch under the cursor unless it is the one playing (an edited sound is never reloaded by passing it).
 * 1 = accepted (or already playing); 0 = refused or busy: still owed, retried on the next scan. */
static int bb_aud_load(void) {
    if(bb_aud_playing()){bb_aud.armed=0;return 1;}
    int r=bb_sound_picker_commit(&bb_sound_pick,&bb_sound,&bb_sound_pick_kits);
    if(r==BB_SP_OK||r==BB_SP_UNCHANGED){bb_aud.armed=0;bb_dirty=1;return 1;}
    if(r==BB_SP_INVALID&&!bb_aud.keep){bb_aud.armed=0;bb_dirty=1;}   /* a settle with nothing to load; KEEP retries */
    return 0;
}
static void bb_aud_detent(void) {
    if(!bb_audition_detent(&bb_aud,fm1_ms))return;
    bb_aud.index=bb_patch_index[0];bb_aud.edited=bb_patch_edited[0];bb_aud.scene=bb_patch_from_scene[0];
    for(unsigned p=0;p<BB_AUD_PARAMS;p++)bb_aud.value[p]=(uint16_t)bb_aud_get(p);
}
static void bb_aud_poll(void) {
    if(bb_aud.restore) {
        if(bb_audition_restore(&bb_aud,bb_aud_get,bb_aud_send)) {
            bb_patch_index[0]=bb_aud.index;bb_patch_edited[0]=bb_aud.edited;bb_patch_from_scene[0]=bb_aud.scene;
            bb_say("SOUND RESTORED");
        }
        return;
    }
    if(!bb_aud_page()){bb_audition_leave(&bb_aud);return;}   /* left another way: the new sound stays */
    if(bb_aud.keep){   /* an owed KEEP: retried every scan until the owner plays the cursor's patch */
        if(bb_audition_keep_step(&bb_aud,bb_sound_pick.pending,!bb_sound_pick.pending&&bb_aud_playing(),bb_aud_load))
            bb_say("SOUND KEPT");
        return;
    }
    if(!bb_audition_due(&bb_aud,fm1_ms,bb_sound_pick_visible(),bb_sound_pick.pending))return;
    (void)bb_aud_load();
}
/* EDIT = KEEP: owed; bb_aud_poll, later in this same scan, looks at what plays and completes it or loads the patch. */
static void bb_aud_keep(void) {bb_audition_keep(&bb_aud);}
