/* Host test for firmware/src/bb_ui_kit_label.h: a drum lane names a sounding sampled kit as the KIT page
 * does, and keeps the native / 909 name otherwise (r9.9; #4 6471457725). */
#include <stdio.h>
#include <string.h>
#include "bb_ui_kit_label.h"
static int checks, fails;
#define EQ(got, want, what) do { checks++; if (strcmp((got), (want))) { fails++; printf("FAIL %s: %s != %s\n", what, got, want); } } while (0)
int main(void) {
    static const char *const names[3] = {"LinnDrum", "DrumTraks", "DMX"};
    const unsigned ID808 = 254;
    EQ(bb_ui_kit_label(0, "808 Reference", names, 3, 1, 0, ID808), "LinnDrum", "sampled LinnDrum");
    EQ(bb_ui_kit_label(0, "808 Reference", names, 3, 1, 1, ID808), "DrumTraks", "sampled DrumTraks");
    EQ(bb_ui_kit_label(0, "808 Tight Hats", names, 3, 1, 2, ID808), "DMX", "sampled DMX over a non-reference native kit");
    EQ(bb_ui_kit_label(0, "808 Long", names, 3, 1, ID808, ID808), "808 Long", "native 808 keeps its kit name");
    EQ(bb_ui_kit_label(1, "808 Reference", names, 3, 1, 0, ID808), "Oh Nine Oh", "909 family wins");
    EQ(bb_ui_kit_label(0, "808 Reference", names, 3, 0, 0, ID808), "808 Reference", "status unavailable: native name");
    EQ(bb_ui_kit_label(0, "808 Reference", names, 3, 1, 3, ID808), "808 Reference", "out-of-range id: native name");
    EQ(bb_ui_kit_label(0, "808 Reference", 0, 0, 1, 0, ID808), "808 Reference", "no sample library: native name");
    printf("kit label: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
