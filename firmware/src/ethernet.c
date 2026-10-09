#include <string.h>
#include "funkotto/ethernet.h"
static bool valid(const uint8_t *p, size_t n) {
    if (!p || n < 14 || n > FO_ETH_MAX) return false;
    unsigned type = (unsigned)p[12] * 256u + p[13];
    /* Ethernet II only. EAPOL belongs to CYW43 authentication, never to USB. */
    return type >= 1536u && type != 0x8100u && type != 0x88a8u && type != 0x888eu;
}
static void flush_rx(struct fo_eth *e) {
    e->status.counters.rx_flushed += e->status.rx_used;
    e->status.rx_used = 0; e->rx_head = 0;
}
void fo_eth_init(struct fo_eth *e) { memset(e, 0, sizeof(*e)); }
void fo_eth_enable(struct fo_eth *e, bool enabled) {
    /* New USB owner: discard old completions and frames, but never reuse tickets. */
    for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
        if (e->tx[i].state == TX_QUEUED || e->tx[i].state == TX_INFLIGHT) ++e->status.counters.tx_aborted;
        e->tx[i].state = TX_FREE;
    }
    e->status.tx_used = 0; flush_rx(e); e->status.enabled = enabled;
}
void fo_eth_link(struct fo_eth *e, bool up, uint32_t epoch, const uint8_t mac[6]) {
    if (e->status.link != up || e->status.epoch != epoch) {
        flush_rx(e);
        for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
            struct fo_tx_slot *s = &e->tx[i];
            if (s->state == TX_QUEUED || s->state == TX_INFLIGHT) {
                s->state = TX_DONE; s->result = NET_ABORTED; s->sdk_error = 0;
                ++e->status.counters.tx_aborted;
            }
        }
    }
    e->status.link = up; e->status.epoch = epoch; memcpy(e->status.mac, mac, 6);
}
enum fo_net_result fo_eth_submit(struct fo_eth *e, const uint8_t *data, size_t len, uint32_t *ticket) {
    if (!valid(data, len) || memcmp(data + 6, e->status.mac, 6)) { ++e->status.counters.tx_invalid; return NET_INVALID; }
    if (!e->status.enabled || !e->status.link) return NET_NO_LINK;
    for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
        struct fo_tx_slot *s = &e->tx[i];
        if (s->state != TX_FREE) continue;
        s->frame.len = (uint16_t)(len < 60 ? 60 : len);
        memcpy(s->frame.data, data, len);
        memset(s->frame.data + len, 0, s->frame.len - len);
        if (++e->next_ticket == 0) ++e->next_ticket;
        *ticket = s->ticket = e->next_ticket; s->state = TX_QUEUED;
        ++e->status.tx_used; ++e->status.counters.tx_queued;
        if (e->status.tx_used > e->status.counters.tx_high_water) e->status.counters.tx_high_water = e->status.tx_used;
        return NET_OK;
    }
    ++e->status.counters.tx_busy; return NET_BUSY;
}
bool fo_eth_claim(struct fo_eth *e, struct fo_frame *frame, uint32_t *ticket) {
    if (!e->status.enabled || !e->status.link) return false;
    struct fo_tx_slot *oldest = NULL;
    for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
        struct fo_tx_slot *s = &e->tx[i];
        if (s->state == TX_QUEUED && (!oldest || (int32_t)(s->ticket - oldest->ticket) < 0)) oldest = s;
    }
    if (!oldest) return false;
    oldest->state = TX_INFLIGHT; *ticket = oldest->ticket; *frame = oldest->frame; return true;
}
void fo_eth_complete(struct fo_eth *e, uint32_t ticket, int32_t sdk_error) {
    for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
        struct fo_tx_slot *s = &e->tx[i];
        if (s->ticket != ticket || s->state != TX_INFLIGHT) continue;
        s->state = TX_DONE; s->sdk_error = sdk_error; s->result = sdk_error ? NET_DRIVER : NET_OK;
        if (sdk_error) ++e->status.counters.tx_error; else ++e->status.counters.tx_ok;
        return;
    }
}
enum fo_net_result fo_eth_result(struct fo_eth *e, uint32_t ticket, int32_t *sdk_error) {
    for (unsigned i = 0; i < FO_ETH_SLOTS; ++i) {
        struct fo_tx_slot *s = &e->tx[i];
        if (s->ticket != ticket || s->state == TX_FREE) continue;
        if (s->state != TX_DONE) return NET_PENDING;
        enum fo_net_result r = s->result; *sdk_error = s->sdk_error;
        s->state = TX_FREE; --e->status.tx_used; return r;
    }
    *sdk_error = 0; return NET_ABORTED;
}
void fo_eth_receive(struct fo_eth *e, const uint8_t *data, size_t len) {
    if (!valid(data, len)) { ++e->status.counters.rx_invalid; return; }
    if (!e->status.enabled || !e->status.link) { ++e->status.counters.rx_inactive; return; }
    if (e->status.rx_used == FO_ETH_SLOTS) { ++e->status.counters.rx_full; return; }
    unsigned tail = (e->rx_head + e->status.rx_used) % FO_ETH_SLOTS;
    e->rx[tail].len = (uint16_t)len; memcpy(e->rx[tail].data, data, len);
    ++e->status.rx_used; ++e->status.counters.rx_queued;
    if (e->status.rx_used > e->status.counters.rx_high_water) e->status.counters.rx_high_water = e->status.rx_used;
}
enum fo_net_result fo_eth_pop(struct fo_eth *e, struct fo_frame *frame) {
    if (!e->status.enabled || !e->status.link) return NET_NO_LINK;
    if (!e->status.rx_used) return NET_EMPTY;
    *frame = e->rx[e->rx_head]; e->rx_head = (e->rx_head + 1u) % FO_ETH_SLOTS;
    --e->status.rx_used; return NET_OK;
}
