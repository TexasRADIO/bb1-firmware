#ifndef BB_FAULT_NOTICE_H
#define BB_FAULT_NOTICE_H
/* Reliability notices (Claude Code, reliability r1). Main-loop display only.
 *
 * STABLE IDS: other modules (navigation, UI v2) may store and display these
 * ids. Never renumber; append new ids at the end. The text is the exact
 * one-line message the boot-28 UI shows through bb_say().
 *
 *   1  CANCELLED BY STOP           a queued kit+pattern request was ended
 *                                  by STOP before its bar boundary
 *   2  AUDIO STOPPED: RENDER FAULT the audio owner stopped the transport
 *                                  after BB_RENDER_FAULT_STOP_MS (46 ms)
 *                                  of consecutive rejected render blocks
 *   3  AUDIO FAULT: SAVE, RESTART  a non-retirable worker fault stopped the
 *                                  transport; audio stays off until reboot,
 *                                  save remains available
 *   4  AUDIO FAULT: REQUEST ENDED  a queued kit/pattern/sound request was
 *                                  ended by an audio fault (never applied)
 */
enum {
    BB_NOTICE_NONE=0,
    BB_NOTICE_CANCELLED_BY_STOP=1,
    BB_NOTICE_RENDER_FAULT_STOP=2,
    BB_NOTICE_WORKER_FAULT_STOP=3,
    BB_NOTICE_REQUEST_FAULT=4,
    BB_NOTICE_COUNT
};
static inline const char *bb_notice_text(unsigned id) {
    return id==BB_NOTICE_CANCELLED_BY_STOP?"CANCELLED BY STOP":
           id==BB_NOTICE_RENDER_FAULT_STOP?"AUDIO STOPPED: RENDER FAULT":
           id==BB_NOTICE_WORKER_FAULT_STOP?"AUDIO FAULT: SAVE, RESTART":
           id==BB_NOTICE_REQUEST_FAULT?"AUDIO FAULT: REQUEST ENDED":"";
}
#endif
