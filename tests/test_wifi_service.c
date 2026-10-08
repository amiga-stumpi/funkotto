/* Run the actual Core-1 service against a deterministic SDK fake, without RF. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdk.h"
#include "../firmware/src/wifi_service.c"
cyw43_t cyw43_state;
static uint64_t clock_ms;
static unsigned inits, stops, joins, lock_depth;
static bool init_fail, link_event;
static int (*scan_cb)(void *, const cyw43_ev_scan_result_t *);
uint64_t time_us_64(void) { return clock_ms * 1000; }
void sleep_ms(unsigned n) { clock_ms += n; }
absolute_time_t make_timeout_time_ms(unsigned n) { return clock_ms + n; }
void critical_section_init(critical_section_t *p) { *p = 0; }
void critical_section_enter_blocking(critical_section_t *p) { (void)p; assert(!lock_depth); ++lock_depth; }
void critical_section_exit(critical_section_t *p) { (void)p; assert(lock_depth == 1); --lock_depth; }
void multicore_launch_core1_with_stack(void (*fn)(void), uint32_t *p, size_t n) { assert(fn && p && n == 8192); }
int cyw43_arch_init_with_country(unsigned country) {
    assert(!lock_depth && country == CYW43_COUNTRY_GERMANY); ++inits;
    struct fo_wifi_status s; fo_wifi_snapshot(&s); assert(s.sdk_busy);
    return init_fail ? -7 : 0;
}
void cyw43_arch_deinit(void) { assert(!lock_depth); ++stops; cyw43_state.itf_state = 0; link_event = false; }
void cyw43_arch_enable_sta_mode(void) { cyw43_state.itf_state = 1; }
void cyw43_arch_poll(void) { if (link_event) { link_event = false; cyw43_cb_tcpip_set_link_up(&cyw43_state, 0); } }
void cyw43_arch_wait_for_work_until(absolute_time_t t) { clock_ms = t; }
int cyw43_wifi_pm(cyw43_t *p, unsigned pm) { assert(p == &cyw43_state && pm == CYW43_NONE_PM); return 0; }
int cyw43_wifi_get_mac(cyw43_t *p, int itf, uint8_t *mac) { (void)p; (void)itf; memcpy(mac, "\x02\x01\x02\x03\x04\x05", 6); return 0; }
int cyw43_wifi_scan(cyw43_t *p, cyw43_wifi_scan_options_t *opts, void *env, int (*cb)(void *, const cyw43_ev_scan_result_t *)) {
    (void)opts; (void)env; p->scan_active = true; scan_cb = cb; return 0;
}
bool cyw43_wifi_scan_active(cyw43_t *p) { return p->scan_active; }
int cyw43_wifi_join(cyw43_t *p, size_t n, const uint8_t *ssid, size_t k, const uint8_t *key, unsigned auth, const uint8_t *bssid, unsigned channel) {
    assert(!lock_depth && n == 1 && ssid[0] == 'T' && k == 8 && key[0] == '1');
    assert(auth == CYW43_AUTH_WPA2_AES_PSK && !bssid && !channel);
    ++joins; p->join_state = 1; return 0;
}
int cyw43_wifi_link_status(cyw43_t *p, int itf) { (void)itf; return p->join_state; }
int cyw43_wifi_get_rssi(cyw43_t *p, int32_t *rssi) { (void)p; *rssi = -42; return 0; }
bool pio_sm_is_claimed(unsigned p, unsigned sm) { return cyw43_state.itf_state && p == 1 && sm == 0; }
bool dma_channel_is_claimed(unsigned c) { return cyw43_state.itf_state && c == 3; }
int main(void) {
    fo_wifi_launch(); service_init();
    assert(model.state == FO_UNCONFIGURED && driver_up && status.mac_valid);
    assert(status.pio_mask == 0x10 && status.dma_mask == 8);
    assert(fo_wifi_submit(REQ_SCAN, NULL)); assert(!fo_wifi_submit(REQ_CONNECT, NULL)); service_step();
    assert(status.scanning && status.reply == REPLY_OK);
    for (unsigned i = 0; i < 35; ++i) {
        cyw43_ev_scan_result_t r = {.ssid_len=1, .ssid={'X'}, .bssid={0}, .rssi=-55};
        r.bssid[5] = (uint8_t)i; scan_cb(NULL, &r);
    }
    assert(status.scan_count == 32 && status.scan_truncated);
    cyw43_ev_scan_result_t duplicate = {.ssid_len=1, .ssid={'Y'}, .bssid={0}, .rssi=-40};
    scan_cb(NULL, &duplicate); assert(status.scan_count == 32 && status.results[0].ssid[0] == 'Y');
    clock_ms += 10001; service_step(); assert(!status.scanning && !driver_up && status.scan_done == 1);
    assert(fo_wifi_submit(REQ_SCAN, NULL)); service_step(); assert(status.scanning && status.sdk_error == 0);
    assert(fo_wifi_submit(REQ_DISCONNECT, NULL)); service_step(); assert(!status.scanning && !driver_up);
    struct fo_profile p = {.ssid={'T'}, .ssid_len=1, .key={'1','2','3','4','5','6','7','8'}, .key_len=8};
    assert(fo_wifi_submit(REQ_SET, &p)); service_step(); assert(model.configured && status.reply == REPLY_OK);
    const uint8_t *secret_copy = (const uint8_t *)&mailbox.profile;
    for (unsigned i = 0; i < sizeof(mailbox.profile); ++i) assert(secret_copy[i] == 0);
    assert(fo_wifi_submit(REQ_CONNECT, NULL)); service_step(); assert(joins == 1 && model.state == FO_CONNECTING);
    link_event = true; service_step(); assert(model.state == FO_LINK_UP && status.rssi_valid);
    assert(fo_wifi_submit(REQ_SCAN, NULL)); service_step(); assert(status.reply == REPLY_BUSY);
    cyw43_cb_process_ethernet(NULL, 0, 64, NULL); cyw43_cb_process_ethernet(NULL, 0, 2000, NULL);
    assert(status.rx_dropped == 1 && status.rx_invalid == 1);
    cyw43_cb_tcpip_set_link_down(&cyw43_state, 0); service_step();
    assert(model.state == FO_RETRY_WAIT && !driver_up);
    cyw43_cb_tcpip_set_link_up(&cyw43_state, 0); service_step(); assert(model.state == FO_RETRY_WAIT);
    clock_ms += 1000; service_step(); assert(joins == 2 && model.state == FO_CONNECTING);
    cyw43_state.join_state = CYW43_LINK_BADAUTH; service_step(); assert(model.error == FO_BAD_AUTH && !driver_up);
    assert(status.last_link_error == CYW43_LINK_BADAUTH && status.failed_attempt == 2);
    assert(fo_wifi_submit(REQ_DISCONNECT, NULL)); service_step();
    clock_ms += 60000; service_step(); assert(joins == 2 && !model.wanted);
    init_fail = true; assert(fo_wifi_submit(REQ_SCAN, NULL)); service_step();
    assert(model.state == FO_ERROR && status.reply == REPLY_DRIVER && status.sdk_error == -7);
    init_fail = false; assert(fo_wifi_submit(REQ_CONNECT, NULL)); service_step();
    assert(model.state == FO_CONNECTING && joins == 3 && !lock_depth && inits >= 5 && stops >= 3);
    assert(status.last_link_error == CYW43_LINK_BADAUTH && status.failed_attempt == 2);
    cyw43_state.join_state = CYW43_LINK_FAIL; service_step();
    assert(model.error == FO_JOIN_FAILED && status.sdk_error == 0);
    assert(status.last_link_error == -1 && status.failed_attempt == 3);
    assert(!strcmp(fo_error_name(model.error), "JOIN_FAILED"));
    assert(fo_wifi_submit(REQ_SET, &p)); service_step();
    assert(status.last_link_error == 0 && status.failed_attempt == 0);
    assert(status.ssid_len == 1 && status.ssid[0] == 'T');
    puts("PASS: actual WLAN service init, scan bounds/cancel/timeout, mailbox wiping, link loss/retry and SDK failure recovery");
}
