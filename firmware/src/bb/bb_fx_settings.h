#ifndef BB_FX_SETTINGS_H
#define BB_FX_SETTINGS_H
#include <stdint.h>
#define BB_FX_PERCENT_MAX 100u
/* Shared effects settings belong to the scene, not an instrument patch.
 * Sends remain independent of note velocity and patch recall. */
typedef enum {
    BB_FX_ECHO_MS, BB_FX_ECHO_FEEDBACK, BB_FX_ECHO_TONE,
    BB_FX_ECHO_DRIVE, BB_FX_ECHO_WEAR, BB_FX_ECHO_WET, BB_FX_ECHO_HEADS,
    BB_FX_SPRING_SIZE, BB_FX_SPRING_DAMP, BB_FX_SPRING_WET,
    BB_FX_DILLO_ECHO, BB_FX_DILLO_SPRING,
    BB_FX_TELEGRAM_ECHO, BB_FX_TELEGRAM_SPRING,
    BB_FX_ECHO_MOTION, BB_FX_PARAMETER_COUNT
} bb_fx_parameter;
typedef struct { uint16_t value[BB_FX_PARAMETER_COUNT]; } bb_fx_settings;
/* BB1 2.3: extended FX state, separate from the v1 session format (fixed at 15 params).
 * These live in bb_core alongside bb_fx_settings and flow through bb_section_bundle
 * for per-song loading. The session serialiser ignores them (defaults to zero = off). */
typedef struct {
    uint8_t delay_type;      /* 0=DIGI, 1=TAPE */
    uint8_t delay_division;  /* 0=free ms, 1..13=note division */
    uint8_t dist_type;       /* 0..6 (Diode..Crush); 255=off */
    uint8_t comp_amount;     /* 0..100, 0=bypass */
} bb_fx_extended;
#endif
