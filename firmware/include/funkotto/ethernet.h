#ifndef FO_ETHERNET_H
#define FO_ETHERNET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FO_ETH_MAX 1514u
#define FO_ETH_SLOTS 8u
#define FO_ETH_BUFFER 1600u
/* Status values are also the USB v1 wire values. */
enum fo_net_result { NET_OK, NET_EMPTY, NET_BUSY, NET_NO_LINK, NET_INVALID,
    NET_ABORTED, NET_DRIVER, NET_PENDING, NET_SESSION, NET_SEQUENCE };
enum fo_tx_state { TX_FREE, TX_QUEUED, TX_INFLIGHT, TX_DONE };
struct fo_frame { uint16_t len; uint8_t data[FO_ETH_BUFFER]; };
struct fo_tx_slot { struct fo_frame frame; uint32_t ticket; enum fo_tx_state state; enum fo_net_result result; int32_t sdk_error; };
struct fo_eth_stats {
    uint32_t tx_queued, tx_ok, tx_error, tx_aborted, tx_busy, tx_invalid;
    uint32_t rx_queued, rx_full, rx_inactive, rx_invalid, rx_flushed;
    uint32_t tx_high_water, rx_high_water;
};
struct fo_eth_status { struct fo_eth_stats counters; uint32_t epoch; uint8_t mac[6]; uint8_t tx_used, rx_used; bool link, enabled; };
struct fo_eth {
    struct fo_tx_slot tx[FO_ETH_SLOTS]; struct fo_frame rx[FO_ETH_SLOTS];
    struct fo_eth_status status; uint32_t next_ticket; unsigned rx_head;
};
/* Pure bounded operations. The owner must hold its cross-core lock. */
void fo_eth_init(struct fo_eth *e);
void fo_eth_enable(struct fo_eth *e, bool enabled);
void fo_eth_link(struct fo_eth *e, bool up, uint32_t epoch, const uint8_t mac[6]);
enum fo_net_result fo_eth_submit(struct fo_eth *e, const uint8_t *data, size_t len, uint32_t *ticket);
bool fo_eth_claim(struct fo_eth *e, struct fo_frame *frame, uint32_t *ticket);
void fo_eth_complete(struct fo_eth *e, uint32_t ticket, int32_t sdk_error);
enum fo_net_result fo_eth_result(struct fo_eth *e, uint32_t ticket, int32_t *sdk_error);
void fo_eth_receive(struct fo_eth *e, const uint8_t *data, size_t len);
enum fo_net_result fo_eth_pop(struct fo_eth *e, struct fo_frame *frame);
/* Thread-safe facade owned by wifi_service; no caller enters CYW43. */
void fo_net_enable(bool enabled);
void fo_net_snapshot(struct fo_eth_status *status);
enum fo_net_result fo_net_submit(const uint8_t *data, size_t len, uint32_t *ticket);
enum fo_net_result fo_net_result(uint32_t ticket, int32_t *sdk_error);
enum fo_net_result fo_net_pop(struct fo_frame *frame);
#endif
