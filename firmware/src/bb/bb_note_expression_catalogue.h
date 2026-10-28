/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_NOTE_EXPRESSION_CATALOGUE_H
#define BB_NOTE_EXPRESSION_CATALOGUE_H
#include "bb_note_expression.h"
#include "bb_catalogue.h"
/* Control-thread only. Caller separately validates the complete note schema.
 * Reads only velocity/gate, retaining presence; failure leaves output intact. */
int bb_note_expression_from_catalogue(const bb_catalogue *,bb_catalogue_id,bb_note_expression *);
#endif
