/* SPDX-License-Identifier: MIT
 * Big Beat target recovery decision: deliberately independent of the hardware HAL. */
#ifndef BB_RECOVERY_POLICY_H
#define BB_RECOVERY_POLICY_H

enum bb_recovery_action {
    BB_RECOVERY_NORMAL = 0,
    BB_RECOVERY_USB_SAFE = 1
};

static inline enum bb_recovery_action bb_recovery_decide(unsigned failed_starts)
{
    return failed_starts >= 2u ? BB_RECOVERY_USB_SAFE : BB_RECOVERY_NORMAL;
}

#endif
