#ifndef BB_FACTORY_H
#define BB_FACTORY_H
#include "bb_catalogue.h"
#include "bb_browser.h"
/* Control/startup only. This is catalogue access, not permission to substitute
 * unsupported voices, timing or GreyHound with something different. */
extern bb_catalogue bb_factory_catalogue;
extern bb_browser bb_factory_browser;
extern unsigned bb_factory_ready;
int bb_factory_init(void);
#endif
