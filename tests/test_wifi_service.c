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
#ifdef FUNKOTTO_W3
static unsigned sends;
int cyw43_send_ethernet(cyw43_t *p, int itf, size_t len, const void *buf, bool is_pbuf) {
    assert(p == &cyw43_state && itf == 0 && !is_pbuf && len == 60 && !lock_depth);
    ++sends;
    /* SDK can reenter receive while sending: verify lock isn't held. */
    cyw43_cb_process_ethernet(NULL, 0, len, buf);
    return 0;
}
#endif
#ifdef FUNKOTTO_W4
static uint8_t flash_bytes[2][4096];
static bool flash_allowed=true, flash_window;
static unsigned flash_ops, flash_inits;
static const uint8_t *flash_read(unsigned slot) { assert(slot<2);return flash_bytes[slot]; }
static bool flash_erase(unsigned slot) {
    assert(flash_window && !driver_up && !lock_depth && !cyw43_state.itf_state && slot<2);
    ++flash_ops;memset(flash_bytes[slot],255,4096);return true;
}
static bool flash_program(unsigned slot,unsigned page,const uint8_t *data) {
    assert(flash_window && !driver_up && !lock_depth && slot<2 && page<2);
    ++flash_ops;for(unsigned i=0;i<256;++i)flash_bytes[slot][page*256+i]&=data[i];return true;
}
const struct fo_store_io fo_flash_io={flash_read,flash_erase,flash_program};
bool fo_flash_core_init(void) { ++flash_inits;return true; }
bool fo_flash_open(void) { assert(!driver_up && !lock_depth);flash_window=flash_allowed;return flash_window; }
void fo_flash_close(void) { flash_window=false; }
static void storage_tests(struct fo_profile *p) {
    assert(flash_inits==2 && status.flash_ready && !status.stored);
    assert(fo_wifi_submit(REQ_SAVE,NULL));assert(fo_wifi_command_busy());service_step();
    assert(status.reply==REPLY_OK && status.stored && status.flash_profile && flash_ops==3 && !fo_wifi_command_busy());
    unsigned ops=flash_ops;assert(fo_wifi_submit(REQ_SAVE,NULL));service_step();assert(flash_ops==ops);
    assert(fo_wifi_submit(REQ_CONNECT,NULL));service_step();link_event=true;service_step();
    assert(model.state==FO_LINK_UP);
    p->key[7]='9';assert(fo_wifi_submit(REQ_SET,p));service_step();assert(!status.stored && status.flash_profile);
    assert(fo_wifi_submit(REQ_CONNECT,NULL));service_step();link_event=true;service_step();
    assert(fo_wifi_submit(REQ_SAVE,NULL));service_step();
    assert(status.reply==REPLY_OK && model.wanted && status.stored && !flash_window);
    link_event=true;service_step();assert(model.state==FO_LINK_UP);
    ops=flash_ops;
    driver_stop();service_init();service_step();
    assert(model.wanted && model.configured && model.profile.key[7]=='9' && status.stored && flash_ops==ops);
    /* Save after failed quiescence must not change flash; retry intent survives. */
    flash_allowed=false;assert(fo_wifi_submit(REQ_SAVE,NULL));service_step();
    assert(status.reply==REPLY_FLASH && model.wanted && flash_ops==ops && status.storage_error==STORE_NOT_READY);
    flash_allowed=true;
    assert(fo_wifi_submit(REQ_ERASE,NULL));service_step();
    assert(status.reply==REPLY_OK && !model.configured && !model.wanted && !status.stored && !status.flash_profile);
    for(unsigned i=0;i<sizeof(model.profile);++i)assert(((uint8_t *)&model.profile)[i]==0);
    driver_stop();service_init();service_step();assert(!model.configured && !model.wanted);
    assert(fo_wifi_submit(REQ_SAVE,NULL));service_step();assert(status.reply==REPLY_NO_PROFILE);
    /* Simulate committed delete with interrupted old-secret cleanup on reboot. */
    assert(fo_wifi_submit(REQ_SET,p));service_step();assert(fo_wifi_submit(REQ_SAVE,NULL));service_step();
    uint8_t old_sector[4096];unsigned old=(unsigned)profile_store.active;memcpy(old_sector,flash_bytes[old],4096);
    assert(fo_wifi_submit(REQ_ERASE,NULL));service_step();memcpy(flash_bytes[old],old_sector,4096);
    driver_stop();service_init();service_step();assert(!model.configured && !model.wanted);
    for(unsigned i=0;i<4096;++i)assert(flash_bytes[old][i]==255);
    assert(fo_wifi_submit(REQ_SCAN,NULL));service_step();ops=flash_ops;
    assert(fo_wifi_submit(REQ_ERASE,NULL));service_step();assert(status.reply==REPLY_BUSY && flash_ops==ops);
    puts("PASS: W4 actual service save/erase, stored-vs-RAM state, boot autoconnect, delete cleanup and failed quiescence");
}
#endif
int main(void) {
#ifdef FUNKOTTO_W4
    memset(flash_bytes,255,sizeof(flash_bytes));
#endif
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
#ifdef FUNKOTTO_W3
    fo_net_enable(true);
    uint8_t packet[60] = {0}; memcpy(packet+6, status.mac, 6); packet[12]=8;
    uint32_t ticket; int32_t sdk_error; struct fo_frame received;
    assert(fo_net_submit(packet, 14, &ticket)==NET_OK);
    service_step(); assert(sends==1 && fo_net_result(ticket, &sdk_error)==NET_OK);
    assert(fo_net_pop(&received)==NET_OK && received.len==60 && !memcmp(packet,received.data,60));
    cyw43_cb_process_ethernet(NULL, 0, 60, packet); packet[14]=0xff;
    assert(fo_net_pop(&received)==NET_OK && received.data[14]==0);
    cyw43_cb_process_ethernet(NULL, 1, 60, packet);
    assert(ethernet.status.counters.rx_invalid==1);
    assert(fo_net_submit(packet,60,&ticket)==NET_OK);
#else
    cyw43_cb_process_ethernet(NULL, 0, 64, NULL); cyw43_cb_process_ethernet(NULL, 0, 2000, NULL);
    assert(status.rx_dropped == 1 && status.rx_invalid == 1);
#endif
    cyw43_cb_tcpip_set_link_down(&cyw43_state, 0); service_step();
    assert(model.state == FO_RETRY_WAIT && !driver_up);
#ifdef FUNKOTTO_W3
    assert(fo_net_result(ticket,&sdk_error)==NET_ABORTED && sends==1);
    assert(!ethernet.status.rx_used && !ethernet.status.link);
#endif
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
#ifdef FUNKOTTO_W4
    storage_tests(&p);
#endif
    puts("PASS: actual WLAN service init, scan bounds/cancel/timeout, mailbox wiping, link loss/retry and SDK failure recovery");
}
