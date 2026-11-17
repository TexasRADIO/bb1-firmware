/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#define BB909_CONTROL_VOICES 12u
#define BB909_CONTROL_POTS 11u
typedef struct {uint8_t value[12][11];uint16_t touched[12];uint32_t revision;} bb909_controls;
unsigned bb909_control_count(unsigned voice);
int bb909_control_max(unsigned voice,unsigned parameter);
const char *bb909_control_name(unsigned voice,unsigned parameter);
const char *bb909_control_value_name(unsigned voice,unsigned parameter,unsigned value);
int bb909_control_valid(unsigned voice,unsigned parameter,unsigned value);
void bb909_controls_default(bb909_controls *);
/* Applied mirror only: call AFTER the matching worker job is accepted.
 * Touched pots are retained even after returning to their default value.
 * Untouched engineering defaults must not be re-applied on restore. */
int bb909_controls_applied(bb909_controls *,unsigned voice,unsigned parameter,unsigned value);
