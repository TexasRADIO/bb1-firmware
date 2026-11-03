/* SPDX-License-Identifier: GPL-3.0-only
 * 2.1 playability: choosing a song always replaces A, B and Dillo.
 * Black 1-3 are deliberately inert on SONGS, ERAS and PRESETS so a mixer-like
 * tap cannot silently leave one player on the previous song.
 * All 3 collections x 8 stale legacy masks x 3 part keys, stopped and running.
 */
#include "bb_nav.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    printf("FAIL line %u collection=%u legacy=%u part=%u running=%u\n", \
           (unsigned)__LINE__, coll, take, part, running); } } while (0)

int main(void)
{
    for (unsigned coll = 0; coll < 3; coll++)
    for (unsigned take = 0; take < 8; take++)
    for (unsigned part = 0; part < 3; part++)
    for (unsigned running = 0; running < 2; running++) {
        bb_nav_state s;
        bb_nav_init(&s, BB_NAV_STUDIO, 600);
        s.bank = BB_NAV_HOME_SONGS;
        bb_nav_in in;
        bb_nav_out out;
        memset(&in, 0, sizeof in);
        memset(&out, 0, sizeof out);
        in.facts.home_coll = (uint8_t)coll;
        in.facts.home_take = (uint8_t)take;
        in.facts.running = (uint8_t)running;
        const uint8_t key = BB_NAV_BLACK(part + 1);
        uint8_t arg = BB_NAV_NONE_IDX;
        CHECK(bb_nav_key_role(&s, key, 0, &arg) == BB_NAV_K_NONE);
        CHECK(bb_nav_key_led(&s, &in.facts, key, 0) == BB_NAV_LED_DARK);
        in.keys = in.keys_held = 1u << key;
        in.now_ms = 1000;
        bb_nav_input(&s, &in, &out);
        CHECK(out.kev_count == 0);
    }
    printf("home all-three: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
