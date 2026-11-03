/* BB1 2.1 top-level SELECT contract. Host-only policy test. */
#include <stdio.h>
#include "bb/bb_top_mode.h"

static unsigned checks, failures;
#define CHECK(x, ...) do { checks++; if (!(x)) { failures++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    CHECK(bb_top_mode_step(BB_TOP_CONDUCTOR, 1) == BB_TOP_BIG_BEAT, "CONDUCTOR +1");
    CHECK(bb_top_mode_step(BB_TOP_BIG_BEAT, 1) == BB_TOP_DILLO, "BIG BEAT +1");
    CHECK(bb_top_mode_step(BB_TOP_DILLO, 1) == BB_TOP_DILLO, "DILLO clamps high");
    CHECK(bb_top_mode_step(BB_TOP_DILLO, -1) == BB_TOP_BIG_BEAT, "DILLO -1");
    CHECK(bb_top_mode_step(BB_TOP_BIG_BEAT, -1) == BB_TOP_CONDUCTOR, "BIG BEAT -1");
    CHECK(bb_top_mode_step(BB_TOP_CONDUCTOR, -1) == BB_TOP_CONDUCTOR, "CONDUCTOR clamps low");
    CHECK(bb_top_mode_step(99, 0) == BB_TOP_CONDUCTOR, "invalid mode returns conductor");
    CHECK(bb_top_mode_step(BB_TOP_CONDUCTOR, 127) == BB_TOP_DILLO, "large positive clamps");
    CHECK(bb_top_mode_step(BB_TOP_DILLO, -127) == BB_TOP_CONDUCTOR, "large negative clamps");

    CHECK(bb_top_mode_home(BB_TOP_CONDUCTOR, 4, 7) == 4, "conductor HOME");
    CHECK(bb_top_mode_home(BB_TOP_BIG_BEAT, 4, 7) == 7, "big beat HOME");
    CHECK(bb_top_mode_home(BB_TOP_DILLO, 4, 7) == 7, "dillo HOME");

    CHECK(bb_top_mode_row(BB_TOP_CONDUCTOR, 2, 1, 2) == 2, "conductor keeps local focus");
    CHECK(bb_top_mode_row(BB_TOP_BIG_BEAT, 2, 0, 2) == 0, "big beat layer A");
    CHECK(bb_top_mode_row(BB_TOP_BIG_BEAT, 2, 1, 2) == 1, "big beat layer B");
    CHECK(bb_top_mode_row(BB_TOP_DILLO, 0, 1, 2) == 2, "dillo row");
    CHECK(bb_top_mode_row(BB_TOP_CONDUCTOR, 9, 0, 2) == 0, "invalid conductor focus");
    CHECK(bb_top_mode_row(BB_TOP_BIG_BEAT, 0, 9, 2) == 0, "invalid big beat layer");

    CHECK(bb_top_mode_name(BB_TOP_CONDUCTOR)[0] == 'R', "conductor name");
    CHECK(bb_top_mode_name(BB_TOP_BIG_BEAT)[0] == 'B', "big beat name");
    CHECK(bb_top_mode_name(BB_TOP_DILLO)[0] == 'D', "dillo name");
    CHECK(bb_top_mode_name(99)[0] == 'R', "invalid name");

    printf("top modes: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
