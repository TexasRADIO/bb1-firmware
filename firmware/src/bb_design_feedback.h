/* Original Big Beat UI logic. Foreground only; no audio or device access. */
#ifndef BB_DESIGN_FEEDBACK_H
#define BB_DESIGN_FEEDBACK_H
#include <stdint.h>

enum { BB_HOME_NONE, BB_HOME_TAP, BB_HELP_OPEN, BB_HELP_CLOSE };
typedef struct { uint32_t since; uint8_t down, help; } bb_home_hold_state;
/* Input must be debounced. Unsigned subtraction handles the millisecond wrap.
 * Call on every foreground scan, including release. Threshold is the existing
 * panel hold preference (300..600ms), not an additional timing setting.
 * Returning TAP only on release avoids navigating before discovering a hold. */
static inline unsigned bb_home_scan(bb_home_hold_state *s, unsigned down,
                                    uint32_t now, uint32_t threshold) {
    down = !!down;
    if (down && !s->down) { s->down=1; s->since=now; return BB_HOME_NONE; }
    if (down && !s->help && now-s->since >= threshold) {
        s->help=1; return BB_HELP_OPEN;
    }
    if (!down && s->down) {
        unsigned held=s->help || now-s->since >= threshold;
        s->down=0; s->help=0;
        return held ? BB_HELP_CLOSE : BB_HOME_TAP;
    }
    return BB_HOME_NONE;
}
/* Focus loss/disconnect must not synthesize a tap. Caller closes overlay. */
static inline void bb_home_cancel(bb_home_hold_state *s) {
    s->down=0; s->help=0;
}

typedef struct {
    bb_home_hold_state home;
    uint32_t blocked_buttons, blocked_notes;
    uint8_t home_ignored;
} bb_help_router;
static inline void bb_help_cancel(bb_help_router *s) {
    bb_home_cancel(&s->home);
    s->home_ignored=1; /* Require release before accepting another gesture. */
}
typedef struct {
    uint32_t pressed, note_edges;
    unsigned home_event, consume_encoders;
} bb_help_route;
/* bb_keys_sync also derives note-ons from raw held state, not just edges.
 * Keep already accepted notes sounding, but block any other held keys during
 * help. Rejected releases remain owned by bb_keys_sent and retry normally. */
static inline uint32_t bb_help_note_block(uint32_t blocked, uint32_t held,
    uint32_t accepted, unsigned help_owns_input) {
    blocked &= held;
    if(help_owns_input) blocked |= held & ~accepted;
    return blocked;
}
/* Adapter contract: call BEFORE ordinary page dispatch, AFTER guaranteed
 * release servicing. Never filter the physical held-state used by releases.
 * home_mask/play_mask come from calibrated panel.btn[], each a single bit.
 * PLAY is always passed through to transport (never interpreted here).
 * HOME owns its down/release cycle; no raw HOME edge reaches legacy handlers.
 * Keys pressed during help stay blocked until released, even after help closes.
 */
static inline bb_help_route bb_help_filter(bb_help_router *s,
    uint32_t buttons, uint32_t notes, uint32_t pressed, uint32_t note_edges,
    uint32_t home_mask, uint32_t play_mask, uint32_t now, uint32_t threshold) {
    unsigned was_down=s->home.down;
    bb_help_route out;
    if(!(buttons&home_mask))s->home_ignored=0;
    else if(!s->home.down && !(pressed&home_mask))s->home_ignored=1;
    out.home_event=s->home_ignored ? BB_HOME_NONE :
        bb_home_scan(&s->home,buttons&home_mask,now,threshold);
    out.consume_encoders=was_down || !!(buttons&home_mask);
    s->blocked_buttons &= buttons;
    s->blocked_notes &= notes;
    if(out.consume_encoders) {
        s->blocked_buttons |= buttons & ~play_mask;
        s->blocked_notes |= note_edges & notes;
    }
    out.pressed=pressed & ~(home_mask | s->blocked_buttons);
    out.note_edges=note_edges & ~s->blocked_notes;
    if(out.consume_encoders) {
        out.pressed &= play_mask;
        out.note_edges=0;
    }
    return out;
}

enum { BB_LIGHT_OFF, BB_LIGHT_DIM, BB_LIGHT_MEDIUM, BB_LIGHT_BRIGHT };
/* Semantic output, NOT an assumed hardware PWM encoding. The target adapter
 * must map through the retained LED driver. Keys are monochrome; instrument
 * colour belongs on the screen. Use the audio cursor, never a UI timer, for
 * tracer. pulse_phase is foreground animation state, not a musical clock. */
static inline unsigned bb_key_light(unsigned available, unsigned selected,
    unsigned hit, unsigned queued, unsigned tracer, unsigned pulse_phase) {
    if (!available) return BB_LIGHT_OFF;
    if (tracer) return BB_LIGHT_BRIGHT;
    if (queued) return pulse_phase ? BB_LIGHT_BRIGHT : BB_LIGHT_MEDIUM;
    if (selected) return BB_LIGHT_BRIGHT;
    if (hit) return BB_LIGHT_MEDIUM;
    return BB_LIGHT_DIM;
}
#endif
