#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "funkotto/wifi_model.h"
#include "funkotto/wifi_console.h"
#include "funkotto/wifi_service.h"
static enum fo_ui_command feed(struct fo_wifi_console *u, const char *s) {
    enum fo_ui_command c = UI_NONE;
    while (*s) { enum fo_ui_command next = fo_ui_feed(u, (uint8_t)*s++); if (next != UI_NONE) c = next; }
    return c;
}
static void parser(void) {
    struct fo_wifi_console u; fo_ui_init(&u);
    assert(feed(&u, "wifi set\r\n") == UI_PROMPT_SSID && u.phase == 1);
    assert(feed(&u, " SSID with spaces \r\n") == UI_PROMPT_KEY && u.phase == 2);
    assert(feed(&u, " a test key \r\n") == UI_PROFILE && !u.phase);
    assert(u.profile.ssid_len == 18 && u.profile.key_len == 12);
    assert(!memcmp(u.profile.key, " a test key ", 12));
    for (unsigned i = 0; i < sizeof(u.line); ++i) assert(u.line[i] == 0);
    assert(feed(&u, "wifi sethex\n") == UI_PROMPT_SSID);
    assert(feed(&u, "0061ff\n") == UI_PROMPT_KEY);
    assert(feed(&u, "12345678\n") == UI_PROFILE);
    assert(u.profile.ssid_len == 3 && u.profile.ssid[0] == 0 && u.profile.ssid[2] == 255);
    assert(feed(&u, "wifi sethex\n123\n") == UI_INVALID && !u.phase);
    assert(feed(&u, "wifi set\nSSID\nshort\n") == UI_INVALID && !u.phase);
    assert(feed(&u, "wifi set\nSSID\nsecret") == UI_PROMPT_KEY);
    assert(fo_ui_feed(&u, 3) == UI_CANCEL);
    const uint8_t *raw = (const uint8_t *)&u;
    for (unsigned i = 0; i < sizeof(u); ++i) assert(!raw[i]);
    assert(feed(&u, "wifi set\n") == UI_PROMPT_SSID);
    for (unsigned i = 0; i < 300; ++i) fo_ui_feed(&u, 'x');
    assert(feed(&u, "wifi connect\n") == UI_INVALID && !u.phase);
    assert(feed(&u, "wifi connect\n") == UI_CONNECT);
    assert(feed(&u, "wifi save\n") == UI_NOT_IMPLEMENTED);
    assert(feed(&u, "wifi set\nS\n1234") == UI_PROMPT_KEY);
    fo_ui_feed(&u, 0); assert(feed(&u, "5678\n") == UI_INVALID);
    assert(feed(&u, "wifi statuz\b s\n") == UI_INVALID);
    uint32_t rng = 73;
    for (unsigned i = 0; i < 100000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        (void)fo_ui_feed(&u, (uint8_t)(rng >> 24));
        assert(u.length < sizeof(u.line)); assert(u.phase <= 2);
    }
    fo_ui_cancel(&u);
    assert(feed(&u, "wifi set\n") == UI_PROMPT_SSID);
    for (unsigned i = 0; i < 32; ++i) fo_ui_feed(&u, 's');
    assert(fo_ui_feed(&u, '\n') == UI_PROMPT_KEY);
    for (unsigned i = 0; i < 63; ++i) fo_ui_feed(&u, 'p');
    assert(fo_ui_feed(&u, '\n') == UI_PROFILE && fo_profile_valid(&u.profile));
}
static void model_tests(void) {
    struct fo_wifi_model m; fo_model_init(&m);
    assert(!fo_model_connect(&m, 0) && m.error == FO_NO_PROFILE);
    struct fo_profile p = {.ssid={'T'}, .key={'1','2','3','4','5','6','7','8'}, .ssid_len=1, .key_len=8};
    assert(fo_model_profile(&m, &p));
    struct fo_profile invalid = p; invalid.key_len = 7;
    assert(!fo_model_profile(&m, &invalid)); assert(m.profile.key_len == 8);
    assert(fo_model_connect(&m, 100)); assert(fo_model_tick(&m, 100));
    uint32_t epoch = m.epoch;
    assert(!fo_model_tick(&m, 15099)); assert(m.state == FO_CONNECTING);
    assert(!fo_model_tick(&m, 15100)); assert(m.state == FO_RETRY_WAIT && m.error == FO_TIMEOUT);
    fo_model_link_up(&m, epoch); assert(m.state == FO_RETRY_WAIT);
    assert(!fo_model_tick(&m, 16099)); assert(fo_model_tick(&m, 16100));
    fo_model_link_up(&m, epoch); assert(m.state == FO_CONNECTING);
    fo_model_link_up(&m, m.epoch); assert(m.state == FO_LINK_UP && m.links == 1);
    assert(fo_model_connect(&m, 17000) && m.state == FO_LINK_UP);
    fo_model_fail(&m, FO_LINK_LOST, 17000);
    assert(m.deadline_ms == 18000);
    static const uint32_t delays[] = {2000,4000,8000,16000,30000,30000};
    for (unsigned i = 0; i < sizeof(delays)/sizeof(delays[0]); ++i) {
        uint64_t now = m.deadline_ms; assert(fo_model_tick(&m, now));
        fo_model_fail(&m, FO_BAD_AUTH, now);
        assert(m.deadline_ms == now + delays[i]);
    }
    fo_model_disconnect(&m); epoch = m.epoch;
    assert(!fo_model_tick(&m, UINT64_C(1)<<40));
    fo_model_link_up(&m, epoch); assert(m.state == FO_DISCONNECTED);
    assert(fo_model_connect(&m, UINT64_C(1)<<40)); assert(fo_model_tick(&m, UINT64_C(1)<<40));
    assert(fo_model_profile(&m, &p)); assert(!m.wanted && m.state == FO_DISCONNECTED);
}
static void watchdog_tests(void) {
    struct fo_wifi_status s = {.heartbeat_ms=100};
    assert(fo_wifi_watchdog_healthy(&s, 5099));
    assert(!fo_wifi_watchdog_healthy(&s, 5100));
    s.sdk_busy = true;
    assert(fo_wifi_watchdog_healthy(&s, 15099));
    assert(!fo_wifi_watchdog_healthy(&s, 15100));
    s.sdk_busy = false; s.heartbeat_ms = UINT32_MAX - 10;
    assert(fo_wifi_watchdog_healthy(&s, 100));
    assert(!fo_wifi_watchdog_healthy(&s, 6000));
}
int main(void) { parser(); model_tests(); watchdog_tests(); puts("PASS: WLAN parser, secrets, boundaries, stale events, timeout/reconnect model"); }
