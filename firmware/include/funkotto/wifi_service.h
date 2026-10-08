#ifndef FO_WIFI_SERVICE_H
#define FO_WIFI_SERVICE_H
#include "funkotto/wifi_model.h"
#define FO_SCAN_MAX 32u
struct fo_scan_result { uint8_t ssid[32], bssid[6], ssid_len, auth; uint16_t channel; int16_t rssi; };
enum fo_request { REQ_SET, REQ_CONNECT, REQ_DISCONNECT, REQ_SCAN };
enum fo_reply { REPLY_OK, REPLY_BUSY, REPLY_NO_PROFILE, REPLY_DRIVER, REPLY_INVALID };
struct fo_wifi_status {
    enum fo_wifi_state state; enum fo_wifi_error error; enum fo_reply reply;
    bool configured, wanted, mac_valid, scanning, scan_truncated, sdk_busy, rssi_valid;
    uint8_t mac[6], ssid[32], ssid_len, scan_count;
    int32_t rssi, sdk_error, last_link_error;
    uint32_t failed_attempt;
    uint32_t heartbeat_ms, attempts, links, epoch, completed, scan_generation, scan_done;
    uint32_t init_ms, join_ms, rx_dropped, rx_invalid, pio_mask, dma_mask;
    uint64_t retry_at_ms;
    struct fo_scan_result results[FO_SCAN_MAX];
};
void fo_wifi_launch(void);
bool fo_wifi_submit(enum fo_request request, const struct fo_profile *profile);
void fo_wifi_snapshot(struct fo_wifi_status *s);
static inline bool fo_wifi_watchdog_healthy(const struct fo_wifi_status *s, uint32_t now_ms) {
    return (uint32_t)(now_ms - s->heartbeat_ms) < (s->sdk_busy ? 15000u : 5000u);
}
#endif
