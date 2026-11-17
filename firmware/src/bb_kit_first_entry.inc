/* Big Beat boot entrance. UI state only; never recall a sound or enable a
 * layer. Called after validated sample startup and panel initialisation. */
static void bb_kit_first_entry(void) {
    bb_face.requested.page=BB_DRUMS;
    bb_face.machine=0;
    bb_sound_pick_page=bb_sound_pick_return=0;
    if(!bb_sound_picker_enabled)return;
    if(bb_sound_pick_open(BB_DRUMS,0)) {
        bb_sound_pick_page=1;
        bb_library_page=0;bb_tone_page=0;
        bb_say("CHOOSE KIT / ALGO: PATTERNS");
    } else {
        /* Retain the playable face if catalogue observation fails. Do not
         * fabricate a loaded label or silently replace the current kit. */
        bb_say("KIT LIST UNAVAILABLE / PRESETS: RETRY");
    }
    bb_dirty=1;
}
