/* Host test for firmware/src/bb_nav_arm.h: an off drum layer switches on only when its confirmed kit or
 * pattern is APPLIED (Duke, #4 6469741984). Covers accepted, rejected, cancelled (STOP), superseded,
 * refused / not-ready / busy (never armed), already-enabled, zero ticket, native sound applied / same /
 * failed / superseded / other layer, and the disabled-but-muted state (mute is never touched). */
#include <stdio.h>
#include "bb_nav_arm.h"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bb_nav_arm_view pat(uint32_t req, uint32_t seen, int applied) { bb_nav_arm_view v = {req, seen, (uint8_t)applied, 0, 0, 0}; return v; }
static bb_nav_arm_view snd(int pending, int mine, int applied) { bb_nav_arm_view v = {0, 0, 0, (uint8_t)pending, (uint8_t)mine, (uint8_t)applied}; return v; }
/* Run scans until a verdict; return it (WAIT if none within n). */
static int run(bb_nav_arm_state *s, const bb_nav_arm_view *vs, int n) {
    for (int i = 0; i < n; i++) { int r = bb_nav_arm_step(s, &vs[i]); if (r != BB_NAV_ARM_WAIT) return r; }
    return BB_NAV_ARM_WAIT;
}
int main(void) {
    bb_nav_arm_state s;
    /* 1 accepted pattern on an off layer: waits for the audio owner, then enables once */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 7);
    { bb_nav_arm_view v[] = {pat(7, 6, 1), pat(7, 6, 1), pat(7, 7, 1)}; CHECK(run(&s, v, 3) == BB_NAV_ARM_ENABLE, "applied pattern enables"); }
    { bb_nav_arm_view v = pat(7, 7, 1); CHECK(bb_nav_arm_step(&s, &v) == BB_NAV_ARM_WAIT && s.kind == BB_NAV_ARM_NONE, "enables only once"); }
    /* 2 rejected by the audio owner (wrong kit voices, timing ...): never enables */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 9);
    { bb_nav_arm_view v[] = {pat(9, 8, 0), pat(9, 9, 0)}; CHECK(run(&s, v, 2) == BB_NAV_ARM_DROP, "rejected pattern drops"); }
    /* 3 cancelled by STOP before its boundary (reported, not applied) */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 11);
    { bb_nav_arm_view v[] = {pat(11, 11, 0)}; CHECK(run(&s, v, 1) == BB_NAV_ARM_DROP, "cancelled pattern drops"); }
    /* 4 superseded by a newer request before the first is reported: the old arm never fires */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 12);
    { bb_nav_arm_view v[] = {pat(12, 11, 1), pat(13, 13, 1)}; CHECK(run(&s, v, 2) == BB_NAV_ARM_DROP, "superseded pattern drops"); }
    /* 5 refused / LIBRARY NOT READY / BUSY confirmations never arm (the caller arms only on OK) */
    s = (bb_nav_arm_state){0};
    { bb_nav_arm_view v[] = {pat(0, 0, 0), pat(5, 5, 1), snd(0, 1, 1)}; CHECK(run(&s, v, 3) == BB_NAV_ARM_WAIT && s.kind == BB_NAV_ARM_NONE, "unarmed layer never enables"); }
    /* 6 a layer that is already on is never armed */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 1, 14); CHECK(s.kind == BB_NAV_ARM_NONE, "enabled layer not armed (pattern)");
    bb_nav_arm_on_sound(&s, 1); CHECK(s.kind == BB_NAV_ARM_NONE, "enabled layer not armed (sound)");
    /* 7 a zero ticket (no request was made) is never armed */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 0); CHECK(s.kind == BB_NAV_ARM_NONE, "zero ticket not armed");
    /* 8 native kit: waits while pending; APPLIED or SAME enables; FAILED / SUPERSEDED / other layer drops */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_sound(&s, 0);
    { bb_nav_arm_view v[] = {snd(1, 1, 0), snd(1, 1, 0), snd(0, 1, 1)}; CHECK(run(&s, v, 3) == BB_NAV_ARM_ENABLE, "applied sound enables"); }
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_sound(&s, 0);
    { bb_nav_arm_view v[] = {snd(0, 1, 0)}; CHECK(run(&s, v, 1) == BB_NAV_ARM_DROP, "failed sound drops"); }
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_sound(&s, 0);
    { bb_nav_arm_view v[] = {snd(0, 0, 1)}; CHECK(run(&s, v, 1) == BB_NAV_ARM_DROP, "sound settled for another layer drops"); }
    /* 9 disabled-but-muted: the verdict is only ENABLE; the logic has no mute input or output, so a mute is
     *    kept (the firmware glue then says LAYER ON, STILL MUTED) */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 21);
    { bb_nav_arm_view v[] = {pat(21, 21, 1)}; CHECK(run(&s, v, 1) == BB_NAV_ARM_ENABLE, "muted layer: enable verdict only"); }
    /* 10 queue full when enabling: the glue's bb_send_machine fails, says LOADED; LAYER STILL OFF and the
     *    arm is already cleared, so nothing is retried behind the player's back */
    s = (bb_nav_arm_state){0}; bb_nav_arm_on_pattern(&s, 0, 22);
    { bb_nav_arm_view v = pat(22, 22, 1); (void)bb_nav_arm_step(&s, &v); CHECK(s.kind == BB_NAV_ARM_NONE, "arm cleared at verdict"); }
    printf("nav arm: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
