/* SPDX-License-Identifier: GPL-3.0-only */
#include "control.h"
int hm_turn(hm_state *s, enum hm_encoder e, int delta, hm_intent *out) {
    if (!s || !out || s->selected >= HM_INSTRUMENTS ||
        e < HM_SELECT || e > HM_K4) return 0;
    hm_intent next = { HM_NONE, s->selected, 0 };
    /* Clamp before addition: extreme encoder values cannot overflow. */
    int step = delta > 127 ? 127 : delta < -127 ? -127 : delta;
    if (step) {
        next.delta = step;
        switch (e) {
        case HM_SELECT: {
            int row = (int)s->selected + step;
            if (row < 0) row = 0;
            if (row >= HM_INSTRUMENTS) row = HM_INSTRUMENTS - 1;
            s->selected = (uint8_t)row;
            next.action = HM_FOCUS;
            next.instrument = s->selected;
            break;
        }
        case HM_PRESETS: next.action = HM_SOUND; break;
        case HM_ALGORITHM: next.action = HM_PATTERN; break;
        case HM_K1: case HM_K2: case HM_K3:
            next.action = HM_LEVEL;
            next.instrument = (uint8_t)(e - HM_K1);
            break;
        case HM_K4: next.action = HM_TEMPO; break;
        }
    }
    *out = next;
    return 1;
}

int hm_available(unsigned mask, unsigned instrument) {
    return instrument < HM_INSTRUMENTS && !!(mask & (1u << instrument));
}
int hm_turn_available(hm_state *s, enum hm_encoder e, int delta,
                      unsigned mask, hm_intent *out) {
    mask &= (1u << HM_INSTRUMENTS) - 1u;
    if (!s || !out || !mask || s->selected >= HM_INSTRUMENTS ||
        e < HM_SELECT || e > HM_K4) return 0;
    unsigned first=0;
    while (!hm_available(mask,first)) ++first;
    if (!hm_available(mask,s->selected)) s->selected=(uint8_t)first;
    if (e == HM_SELECT) {
        int step=delta>127?127:delta< -127?-127:delta;
        unsigned row=s->selected;
        int remaining=step<0?-step:step;
        while (remaining--) {
            int next=(int)row+(step<0?-1:1);
            while (next>=0 && next<HM_INSTRUMENTS &&
                   !hm_available(mask,(unsigned)next)) next+=step<0?-1:1;
            if (next<0 || next>=HM_INSTRUMENTS) break;
            row=(unsigned)next;
        }
        s->selected=(uint8_t)row;
        *out=(hm_intent){step?HM_FOCUS:HM_NONE,s->selected,step};
        return 1;
    }
    if (e>=HM_K1 && e<=HM_K3 && !hm_available(mask,(unsigned)(e-HM_K1))) {
        *out=(hm_intent){HM_NONE,(uint8_t)(e-HM_K1),0};
        return 1; /* consume, never fall through to an instrument handler */
    }
    return hm_turn(s,e,delta,out);
}
