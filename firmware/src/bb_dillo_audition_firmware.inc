                                        
                                                                                                                   
                                                                                              
                                                                                                                    
                                                                                                                       
                                                                                                                      
                                                                                                                    
                                                                                                                  
                                                                                                                  
                                                                                                                  
                                                                                                                 
                                                           
                                                                                           
#include "bb_dillo_audition.h"
static bb_audition bb_aud;
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
