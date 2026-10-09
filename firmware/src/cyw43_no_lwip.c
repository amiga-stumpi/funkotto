#include <stdint.h>
#include "pico/stdlib.h"
/* The pinned CYW43 raw-send function still links this symbol even with
 * CYW43_LWIP=0. All FunkOtto callers pass is_pbuf=false. Do not fake a pbuf
 * copy or pull in an IP stack: an accidental pbuf call is a fatal contract
 * violation, with the bus locked and the watchdog eventually resetting. */
struct pbuf;
uint16_t pbuf_copy_partial(const struct pbuf *p, void *destination, uint16_t len, uint16_t offset) {
    (void)p; (void)destination; (void)len; (void)offset;
    panic("FunkOtto: pbuf TX is unsupported");
}
