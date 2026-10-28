/* Link stubs for the sound-picker host fixture: Telegram is absent from this product, and the
 * fixture observes picker state only, so a command push simply succeeds. */
#include "bb_core.h"
#include "bb_control.h"
const bb_named_telegram bb_telegram_bank[BB_TELEGRAM_PATCHES] = {0};
int bb_command_push(bb_commands *c, const bb_command *cmd) { (void)c; (void)cmd; return 1; }
