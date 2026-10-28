#include "bb_factory_pattern.h"
#include "bb_note_expression_catalogue.h"
#include <string.h>
static int key_is(const bb_catalogue *c, bb_catalogue_id id, const char *wanted) {
    const uint8_t *p; size_t n, length = 0;
    while (wanted[length]) length++;
    return bb_catalogue_text(c, id, &p, &n) && n == length && !memcmp(p, wanted, n);
}
static int integer(const bb_catalogue *c, bb_catalogue_id id, unsigned limit, unsigned *out) {
    uint32_t value;
    if (!bb_catalogue_unsigned(c, id, limit, &value)) return 0;
    *out = (unsigned)value; return 1;
}
static int boolean(const bb_catalogue *c, bb_catalogue_id id, uint8_t *out) {
    int kind = bb_catalogue_kind(c, id);
    if (kind != BB_CAT_FALSE && kind != BB_CAT_TRUE) return 0;
    *out = (uint8_t)(kind == BB_CAT_TRUE); return 1;
}
int bb_factory_prepare_synth_data(const bb_catalogue *c, const bb_factory_choice *choice,
                                 unsigned synth, bb_pattern_data *out) {
    if (!out || synth >= BB_SYNTHS || !bb_catalogue_private(c,out,sizeof *out) ||
        !bb_catalogue_private(c,choice,sizeof *choice) || !bb_factory_choice_valid(c, choice)) return BB_FACTORY_PATTERN_INVALID;
    bb_catalogue_id sequence = bb_catalogue_find(c, choice->part, synth ? "lead" : "bass");
    size_t count = bb_catalogue_count(c, sequence);
    if (bb_catalogue_kind(c, sequence) != BB_CAT_ARRAY) return BB_FACTORY_PATTERN_INVALID;
    if (!count || count > BB_STEPS) return BB_FACTORY_PATTERN_LENGTH;
    unsigned declared;
    bb_catalogue_id lengths = bb_catalogue_find(c, choice->part, "lengths");
    if (!integer(c, bb_catalogue_find(c, lengths, synth ? "telegram" : "dillo"), BB_STEPS, &declared) || declared != count) return BB_FACTORY_PATTERN_LENGTH;
    bb_pattern_data data; memset(&data, 0, sizeof data);
    bb_note *notes=data.notes;
    for (unsigned i = 0; i < count; i++) {
        bb_catalogue_id item = bb_catalogue_at(c, sequence, i);
        if (bb_catalogue_kind(c, item) != BB_CAT_OBJECT) return BB_FACTORY_PATTERN_INVALID;
        for (unsigned key = 0; key < bb_catalogue_count(c, item); key++) {
            bb_catalogue_id name = bb_catalogue_key_at(c, item, key);
            if (key_is(c, name, "offset") ||
                key_is(c, name, "ratchet") || key_is(c, name, "hold")) return BB_FACTORY_PATTERN_EXPRESSION;
            if (!key_is(c, name, "note") && !key_is(c, name, "on") &&
                !key_is(c, name, "accent") && !key_is(c, name, "slide") &&
                !key_is(c,name,"velocity") && !key_is(c,name,"gate")) return BB_FACTORY_PATTERN_FIELD;
        }
        unsigned note;
        if (!integer(c, bb_catalogue_find(c, item, "note"), 127, &note) ||
            !boolean(c, bb_catalogue_find(c, item, "on"), &notes[i].gate) ||
            !boolean(c, bb_catalogue_find(c, item, "accent"), &notes[i].accent) ||
            !boolean(c, bb_catalogue_find(c, item, "slide"), &notes[i].slide)) return BB_FACTORY_PATTERN_INVALID;
        notes[i].note = (uint8_t)note;
        bb_note_expression_view expression;
        if(!bb_note_expression_from_catalogue(c,item,&data.expression[i])||
           !bb_note_expression_read(data.expression[i],&expression)||
           (expression.has_velocity&&!expression.velocity)||
           (expression.has_gate&&!expression.gate_percent))return BB_FACTORY_PATTERN_EXPRESSION;
    }
    data.length=(uint8_t)count;*out=data;
    return BB_FACTORY_PATTERN_OK;
}
int bb_factory_prepare_synth_pattern(const bb_catalogue *c,const bb_factory_choice *choice,
                                    unsigned synth,bb_core *staging) {
    if(!staging)return BB_FACTORY_PATTERN_INVALID;
    bb_pattern_data data;
    int result=bb_factory_prepare_synth_data(c,choice,synth,&data);
    if(result!=BB_FACTORY_PATTERN_OK)return result;
    /* This legacy core-only helper has nowhere to retain the sidecar. Never
     * claim compatibility by discarding it; use copied pattern delivery. */
    for(unsigned i=0;i<BB_STEPS;i++)if(data.expression[i])return BB_FACTORY_PATTERN_EXPRESSION;
    memcpy(staging->synths[synth].notes,data.notes,sizeof data.notes);
    staging->synths[synth].length = data.length;
    staging->synths[synth].next = 0;
    return BB_FACTORY_PATTERN_OK;
}
