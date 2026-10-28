/* SPDX-License-Identifier: GPL-3.0-only */
enum {BB_C1_SCENE_REJECTED,BB_C1_SCENE_OK,BB_C1_SCENE_HELD_FAILURE};
static int bb_c1_family_scene_restore(bb_audio *a,const bb_family_scene *scene,const uint8_t *mix) {
    bb_single_pause p=bb_single_target_pause();return bb_single_restore(a,scene,mix,&p);
}
