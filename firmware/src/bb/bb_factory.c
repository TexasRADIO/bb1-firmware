#include "bb_factory.h"
#include "bb_performance_catalogue.inc"

bb_catalogue bb_factory_catalogue;
bb_browser bb_factory_browser;
unsigned bb_factory_ready;
/* One main/control owner; audio never accesses this workspace. */
static bb_pc_cache bb_factory_cache;

int bb_factory_init(void) {
    bb_catalogue candidate;
    bb_factory_ready = 0;
    if (!bb_catalogue_open(&candidate, bb_performance_catalogue,
                           sizeof bb_performance_catalogue,&bb_factory_cache)) return 0;
    bb_catalogue_id eras = bb_catalogue_find(&candidate, (bb_catalogue_id)candidate.container.root, "eras");
    bb_catalogue_id songs = bb_catalogue_find(&candidate, (bb_catalogue_id)candidate.container.root, "songs");
    if (bb_catalogue_kind(&candidate, eras) != BB_CAT_ARRAY ||
        bb_catalogue_kind(&candidate, songs) != BB_CAT_ARRAY) return 0;
    bb_browser browser;
    if (!bb_browser_init(&browser, &candidate)) return 0;
    bb_factory_catalogue = candidate;
    /* Validation used a stack-local catalogue view, not a lasting pointer.
     * Rebind only after publishing the validated global view before audio. */
    browser.catalogue = &bb_factory_catalogue;
    bb_factory_browser = browser;
    bb_factory_ready = 1;
    return 1;
}
