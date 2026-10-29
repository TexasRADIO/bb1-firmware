#include "bb_browser.h"
#include <string.h>
static const char *const groups[] = {"eras", "songs", "acid"};
static bb_catalogue_id list(const bb_catalogue *c, unsigned group) {
    return c && group < BB_BROWSER_GROUPS ? bb_catalogue_find(c, (bb_catalogue_id)c->container.root, groups[group]) : BB_CATALOGUE_NONE;
}
static bb_catalogue_id bank(const bb_catalogue *c, unsigned group, size_t index) {
    return bb_catalogue_at(c, list(c, group), index);
}
static bb_catalogue_id parts(const bb_catalogue *c, bb_catalogue_id b) {
    return bb_catalogue_find(c, b, "parts");
}
static int text(const bb_catalogue *c, bb_catalogue_id id) {
    const uint8_t *p; size_t n;
    return bb_catalogue_text(c, id, &p, &n) && n > 0;
}
static int same_text(const bb_catalogue *c, bb_catalogue_id a, bb_catalogue_id b) {
    const uint8_t *x, *y; size_t nx, ny;
    return bb_catalogue_text(c, a, &x, &nx) && bb_catalogue_text(c, b, &y, &ny) && nx == ny && !memcmp(x, y, nx);
}
static int initial(const bb_catalogue *c, unsigned group, bb_catalogue_id b, unsigned *out) {
    bb_catalogue_id seq = parts(c, b);
    size_t count = bb_catalogue_count(c, seq);
    if (!count || count > UINT16_MAX || bb_catalogue_kind(c, seq) != BB_CAT_ARRAY) return 0;
    if (group == BB_BROWSER_ERAS) {
        uint32_t n;
        if (!bb_catalogue_unsigned(c, bb_catalogue_find(c, b, "initialPart"), (uint32_t)count - 1u, &n)) return 0;
        *out = (unsigned)n; return 1;
    }
    bb_catalogue_id wanted = bb_catalogue_find(c, b, "initialPartId");
    if (!text(c, wanted)) return 0;
    for (unsigned i = 0; i < count; i++) {
        if (same_text(c, wanted, bb_catalogue_find(c, bb_catalogue_at(c, seq, i), "id"))) { *out = i; return 1; }
    }
    return 0;
}
static int ready(const bb_browser *v) {
    if (!v || v->ready != 1 || !v->catalogue || v->group >= BB_BROWSER_GROUPS ||
        !bb_catalogue_private(v->catalogue,v,sizeof *v)) return 0;
    size_t count = bb_catalogue_count(v->catalogue, list(v->catalogue, v->group));
    if (!count || v->bank_index >= count || v->bank_page >= (count + BB_BROWSER_BANK_KEYS - 1u) / BB_BROWSER_BANK_KEYS) return 0;
    size_t sections = bb_catalogue_count(v->catalogue, parts(v->catalogue, bank(v->catalogue, v->group, v->bank_index)));
    return sections && v->part_index < sections && v->part_page < (sections + BB_BROWSER_PART_KEYS - 1u) / BB_BROWSER_PART_KEYS;
}
static int choose(bb_browser *v, unsigned index) {
    unsigned part; bb_catalogue_id b = bank(v->catalogue, v->group, index);
    if (!initial(v->catalogue, v->group, b, &part)) return 0;
    v->bank_index = (uint16_t)index; v->part_index = (uint16_t)part;
    v->bank_page = (uint16_t)(index / BB_BROWSER_BANK_KEYS);
    v->part_page = (uint16_t)(part / BB_BROWSER_PART_KEYS); return 1;
}
int bb_browser_init(bb_browser *out, const bb_catalogue *c) {
    if (!out || !c || !c->container.bytes || !bb_catalogue_private(c,out,sizeof *out)) return 0;
    /* Validate every navigable name, section list and initial identity. A
     * malformed bank never becomes an apparently selectable empty cell. */
    for (unsigned group = 0; group < BB_BROWSER_GROUPS; group++) {
        bb_catalogue_id seq = list(c, group); size_t count = bb_catalogue_count(c, seq);
        if (group == BB_BROWSER_ACID && seq == BB_CATALOGUE_NONE) continue;   /* optional */
        if (bb_catalogue_kind(c, seq) != BB_CAT_ARRAY || !count || count > UINT16_MAX) return 0;
        for (unsigned i = 0; i < count; i++) {
            bb_catalogue_id b = bb_catalogue_at(c, seq, i); unsigned first;
            if (!text(c, bb_catalogue_find(c, b, group ? "song" : "name")) || !initial(c, group, b, &first)) return 0;
            bb_catalogue_id p = parts(c, b);
            for (unsigned j = 0; j < bb_catalogue_count(c, p); j++) {
                bb_catalogue_id item = bb_catalogue_at(c, p, j);
                if (bb_catalogue_kind(c, item) != BB_CAT_OBJECT || !text(c, bb_catalogue_find(c, item, "name"))) return 0;
            }
        }
    }
    bb_browser v; memset(&v, 0, sizeof v); v.catalogue = c; v.ready = 1;
    if (!choose(&v, 0)) return 0;
    *out = v; return 1;
}
size_t bb_browser_bank_count(const bb_browser *v) { return ready(v) ? bb_catalogue_count(v->catalogue, list(v->catalogue, v->group)) : 0; }
size_t bb_browser_part_count(const bb_browser *v) { return ready(v) ? bb_catalogue_count(v->catalogue, parts(v->catalogue, bank(v->catalogue, v->group, v->bank_index))) : 0; }
size_t bb_browser_bank_pages(const bb_browser *v) { return (bb_browser_bank_count(v) + BB_BROWSER_BANK_KEYS - 1u) / BB_BROWSER_BANK_KEYS; }
size_t bb_browser_part_pages(const bb_browser *v) { return (bb_browser_part_count(v) + BB_BROWSER_PART_KEYS - 1u) / BB_BROWSER_PART_KEYS; }
static unsigned moved(unsigned at, int delta, size_t count) {
    /* count <=65535; modulo first avoids overflow even for INT_MIN/MAX.
     * Only 32-bit arithmetic: no unsupported target 64-bit divide helper. */
    int n = (int)count, index = (int)at + delta % n;
    if (index < 0) index += n;
    else if (index >= n) index -= n;
    return (unsigned)index;
}
int bb_browser_group(bb_browser *v, unsigned group) {
    if (!ready(v) || group >= BB_BROWSER_GROUPS) return 0;
    if (v->group == group) return 1;
    bb_browser next = *v; next.group = (uint8_t)group;
    if (!choose(&next, 0)) return 0;
    *v = next; return 1;
}
int bb_browser_move_bank(bb_browser *v, int delta) {
    size_t count = bb_browser_bank_count(v);
    return count ? choose(v, moved(v->bank_index, delta, count)) : 0;
}
int bb_browser_move_bank_page(bb_browser *v, int delta) {
    size_t count = bb_browser_bank_pages(v); if (!count) return 0;
    v->bank_page = (uint16_t)moved(v->bank_page, delta, count); return 1;
}
int bb_browser_move_part_page(bb_browser *v, int delta) {
    size_t count = bb_browser_part_pages(v); if (!count) return 0;
    v->part_page = (uint16_t)moved(v->part_page, delta, count); return 1;
}
bb_catalogue_id bb_browser_bank_cell(const bb_browser *v, unsigned key) {
    if (!ready(v) || key >= BB_BROWSER_BANK_KEYS) return BB_CATALOGUE_NONE;
    return bank(v->catalogue, v->group, (size_t)v->bank_page * BB_BROWSER_BANK_KEYS + key);
}
bb_catalogue_id bb_browser_part_cell(const bb_browser *v, unsigned key) {
    if (!ready(v) || key >= BB_BROWSER_PART_KEYS) return BB_CATALOGUE_NONE;
    return bb_catalogue_at(v->catalogue, parts(v->catalogue, bank(v->catalogue, v->group, v->bank_index)), (size_t)v->part_page * BB_BROWSER_PART_KEYS + key);
}
int bb_browser_current(const bb_browser *v, bb_factory_choice *out) {
    if (!ready(v) || !out || !bb_catalogue_private(v->catalogue,out,sizeof *out)) return 0;
    bb_factory_choice choice; memset(&choice, 0, sizeof choice);
    choice.catalogue = v->catalogue;
    choice.bank_index = v->bank_index; choice.part_index = v->part_index; choice.group = v->group;
    choice.bank = bank(v->catalogue, choice.group, choice.bank_index);
    choice.part = bb_catalogue_at(v->catalogue, parts(v->catalogue, choice.bank), choice.part_index);
    if (!bb_factory_choice_valid(v->catalogue, &choice)) return 0;
    *out = choice; return 1;
}
int bb_browser_bank_key(bb_browser *v, unsigned key, bb_factory_choice *out) {
    if(!v || !v->catalogue || (out && !bb_catalogue_private(v->catalogue,out,sizeof *out)))return 0;
    if (bb_browser_bank_cell(v, key) == BB_CATALOGUE_NONE) return 0;
    if (!choose(v, v->bank_page * BB_BROWSER_BANK_KEYS + key)) return 0;
    return !out || bb_browser_current(v, out);
}
int bb_browser_part_key(bb_browser *v, unsigned key, bb_factory_choice *out) {
    if(!v || !v->catalogue || (out && !bb_catalogue_private(v->catalogue,out,sizeof *out)))return 0;
    if (bb_browser_part_cell(v, key) == BB_CATALOGUE_NONE) return 0;
    v->part_index = (uint16_t)(v->part_page * BB_BROWSER_PART_KEYS + key);
    return !out || bb_browser_current(v, out);
}
int bb_browser_bank_title(const bb_browser *v, unsigned key, char *out, size_t size) {
    bb_catalogue_id b = bb_browser_bank_cell(v, key);
    return b != BB_CATALOGUE_NONE && bb_catalogue_copy_text(v->catalogue, bb_catalogue_find(v->catalogue, b, v->group ? "song" : "name"), out, size);
}
int bb_browser_part_title(const bb_browser *v, unsigned key, char *out, size_t size) {
    bb_catalogue_id p = bb_browser_part_cell(v, key);
    return p != BB_CATALOGUE_NONE && bb_catalogue_copy_text(v->catalogue, bb_catalogue_find(v->catalogue, p, "name"), out, size);
}
int bb_factory_choice_valid(const bb_catalogue *c, const bb_factory_choice *choice) {
    if (!c || !choice || !bb_catalogue_private(c,choice,sizeof *choice) || choice->catalogue != c || choice->group >= BB_BROWSER_GROUPS) return 0;
    bb_catalogue_id b = bank(c, choice->group, choice->bank_index);
    if (b == BB_CATALOGUE_NONE || b != choice->bank) return 0;
    bb_catalogue_id p = bb_catalogue_at(c, parts(c, b), choice->part_index);
    return p != BB_CATALOGUE_NONE && p == choice->part;
}
