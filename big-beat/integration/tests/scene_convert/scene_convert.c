/* SPDX-License-Identifier: GPL-3.0-only
 * 1.2 scene conversion gate (r9.14 step 2, #64 6492165998 §2). Scenes saved against BB1 1.1's library
 * (header aab68e96, payload f5f132ff) must load under a new library that declares 1.1's library its predecessor,
 * with kit IDs inside 1.1's three kits, and come back pinned to the NEW library (the next save is current).
 * Everything else must still be refused: an unknown library, a kit beyond the predecessor's three, no predecessor
 * declared, a tampered scene, and the predecessor declared with 0 kits. Single (v1 sample, v2 808) and pair (v3)
 * scenes, and the older single format through the pair decoder. Host only. */
#include <stdio.h>
#include <string.h>
#include "bb/sample_scene.h"
#include "bb/bb_core.h"
/* every scene here is all-808 (no 909 layer), so the 909 parameter table is never read */
const void *drum909_param(int voice, int i);
const void *drum909_param(int voice, int i) { (void)voice; (void)i; return 0; }
int drum909_nparams(int voice);
int drum909_nparams(int voice) { (void)voice; return 0; }

uint32_t bb909_request_check(const bb909_request *r) { (void)r; return 0; }
enum { OLD_H = 0xaab68e96u, OLD_P = 0xf5f132ffu, NEW_H = 0x11112222u, NEW_P = 0x33334444u, OTHER_H = 0x5555aaaau };
static unsigned checks, failures;
static void expect(int ok, const char *what) { checks++; if (!ok) { failures++; printf("FAIL: %s\n", what); } }
static bb_family_scene scene; static bb_family_scene_workspace work;
static uint8_t mix[16], buf[4096];
static size_t single(uint32_t h, uint32_t p, unsigned kit, unsigned kind)
{
    bb_sample_scene_identity id = { h, p, 0.5f, (uint8_t)(kind ? 0 : kit), 64, 48, (uint8_t)kind };
    return bb_sample_scene_encode(&scene, mix, &id, buf, sizeof buf);
}
static size_t pair(uint32_t h, uint32_t p, unsigned a, unsigned b)
{
    bb_sample_scene_identity id[2] = { { h, p, 0.5f, (uint8_t)a, 64, 48, 0 }, { h, p, 0.25f, (uint8_t)b, 30, 20, 0 } };
    return bb_sample_pair_scene_encode(&scene, mix, id, buf, sizeof buf);
}
static int dec1(size_t n, unsigned count, bb_sample_scene_identity *out)
{ uint8_t m[16]; return bb_sample_scene_decode(buf, n, 44100, NEW_H, NEW_P, count, &work, m, out); }
static int dec2(size_t n, unsigned count, bb_sample_scene_identity out[2])
{ uint8_t m[16]; return bb_sample_pair_scene_decode(buf, n, 44100, NEW_H, NEW_P, count, &work, m, out); }
int main(void)
{
    if (!bb_init(&scene.core, 44100)) { puts("init failed"); return 2; }
    memset(mix, 127, sizeof mix); mix[3] = 90;
    bb_sample_scene_identity id, ids[2]; size_t n;
    for (int pred = 0; pred < 3; pred++) {   /* 0: none declared; 1: 1.1 declared with 3 kits; 2: declared with 0 */
        bb_sample_scene_set_predecessor(OLD_H, OLD_P, pred == 1 ? 3u : 0u);
        for (unsigned kit = 0; kit < 7; kit++) {
            n = single(OLD_H, OLD_P, kit, 0); expect(n > 0, "encode single");
            int ok = dec1(n, 7, &id), want = pred == 1 && kit < 3;
            expect(ok == want, "single v1 from the 1.1 library: accepted only with the predecessor and kit < 3");
            if (ok) expect(id.pack_header_crc == NEW_H && id.pack_payload_crc == NEW_P && id.kit == kit && id.gain == 0.5f,
                           "converted identity is pinned to the new library, kit and gain kept");
            n = pair(OLD_H, OLD_P, kit, (kit + 1) % 7);
            ok = dec2(n, 7, ids); want = pred == 1 && kit < 3 && (kit + 1) % 7 < 3;
            expect(ok == want, "pair v3 from the 1.1 library: both kits must be inside the predecessor's three");
            if (ok) expect(ids[0].pack_header_crc == NEW_H && ids[1].pack_payload_crc == NEW_P && ids[1].kit == (kit + 1) % 7, "pair pinned");
            n = single(OLD_H, OLD_P, kit, 0);   /* the older single format through the pair decoder */
            ok = dec2(n, 7, ids); want = pred == 1 && kit < 3;
            expect(ok == want, "v1 via the pair decoder");
            if (ok) expect(ids[1].pack_header_crc == NEW_H && ids[1].kind == 1, "B owner keeps native 808, pinned to the new library");
        }
        n = single(OLD_H, OLD_P, 0, 1);   /* 808 selection saved while the 1.1 library was present */
        expect(dec1(n, 7, &id) == (pred == 1), "v2 808 scene from the 1.1 library");
        n = single(NEW_H, NEW_P, 6, 0); expect(dec1(n, 7, &id) == 1 && id.kit == 6, "a current scene is unaffected");
        n = single(NEW_H, NEW_P, 6, 0); expect(dec1(n, 6, &id) == 0, "a current scene beyond the installed kits is refused");
        n = single(OTHER_H, OLD_P, 0, 0); expect(dec1(n, 7, &id) == 0, "a scene from an unknown library is refused");
        n = single(OLD_H, NEW_P, 0, 0); expect(dec1(n, 7, &id) == 0, "a half-matching pin is refused");
        n = single(OLD_H, OLD_P, 1, 0); buf[40] ^= 1; expect(dec1(n, 7, &id) == 0, "a tampered scene is refused");
        n = single(OLD_H, OLD_P, 1, 0); buf[16] ^= 1; expect(dec1(n, 7, &id) == 0, "a re-pinned scene fails its own CRC");
    }
    bb_sample_scene_set_predecessor(OLD_H, OLD_P, 3);   /* under the 1.1 library itself nothing changes */
    n = single(OLD_H, OLD_P, 2, 0);
    { uint8_t m[16]; expect(bb_sample_scene_decode(buf, n, 44100, OLD_H, OLD_P, 3, &work, m, &id) == 1 && id.kit == 2 &&
                            id.pack_header_crc == OLD_H, "1.1 scene under the 1.1 library"); }
    printf("scene conversion: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
