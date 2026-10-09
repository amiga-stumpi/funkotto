#include <string.h>
#include "funkotto/wifi_console.h"
void fo_ui_init(struct fo_wifi_console *u) { memset(u, 0, sizeof(*u)); }
void fo_ui_cancel(struct fo_wifi_console *u) { fo_wipe(u, sizeof(*u)); }
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static enum fo_ui_command line(struct fo_wifi_console *u) {
    if (u->invalid) { u->phase = 0; fo_wipe(&u->profile, sizeof(u->profile)); return UI_INVALID; }
    if (u->phase == 1) {
        size_t n = u->length;
        if (u->hex) {
            if (!n || n > 64 || n % 2) goto bad;
            for (size_t i = 0; i < n; i += 2) {
                int a = hex_digit(u->line[i]), b = hex_digit(u->line[i+1]);
                if (a < 0 || b < 0) goto bad;
                u->profile.ssid[i/2] = (uint8_t)(a * 16 + b);
            }
            n /= 2;
        } else {
            if (!n || n > 32) goto bad;
            memcpy(u->profile.ssid, u->line, n);
        }
        u->profile.ssid_len = (uint8_t)n; u->phase = 2; return UI_PROMPT_KEY;
    }
    if (u->phase == 2) {
        if (u->length < 8 || u->length > 63) goto bad;
        memcpy(u->profile.key, u->line, u->length); u->profile.key_len = (uint8_t)u->length;
        if (!fo_profile_valid(&u->profile)) goto bad;
        u->phase = 0; return UI_PROFILE;
    }
    if (!u->length) return UI_NONE;
#ifdef FUNKOTTO_W3
    if (!strcmp(u->line, "raw on")) return UI_RAW_ON;
#endif
    if (!strcmp(u->line, "help")) return UI_HELP;
    if (!strcmp(u->line, "info")) return UI_INFO;
    if (!strcmp(u->line, "status")) return UI_STATUS;
    if (!strcmp(u->line, "wifi status")) return UI_WIFI_STATUS;
    if (!strcmp(u->line, "wifi scan")) return UI_SCAN;
    if (!strcmp(u->line, "wifi results")) return UI_RESULTS;
    if (!strcmp(u->line, "wifi connect")) return UI_CONNECT;
    if (!strcmp(u->line, "wifi disconnect")) return UI_DISCONNECT;
    if (!strcmp(u->line, "wifi stats")) return UI_STATS;
#ifdef FUNKOTTO_W4
    if (!strcmp(u->line, "wifi save")) return UI_SAVE;
    if (!strcmp(u->line, "wifi erase")) return UI_ERASE;
#else
    if (!strcmp(u->line, "wifi save") || !strcmp(u->line, "wifi erase")) return UI_NOT_IMPLEMENTED;
#endif
    if (!strcmp(u->line, "wifi set") || !strcmp(u->line, "wifi sethex")) {
        fo_wipe(&u->profile, sizeof(u->profile));
        u->hex = !strcmp(u->line, "wifi sethex"); u->phase = 1; return UI_PROMPT_SSID;
    }
    return UI_INVALID;
bad:
    u->phase = 0; fo_wipe(&u->profile, sizeof(u->profile)); return UI_INVALID;
}
enum fo_ui_command fo_ui_feed(struct fo_wifi_console *u, uint8_t c) {
    if (c == 3) { fo_ui_cancel(u); return UI_CANCEL; }
    if (c == '\n' && u->skip_lf) { u->skip_lf = false; return UI_NONE; }
    u->skip_lf = c == '\r';
    if (c == '\r' || c == '\n') {
        enum fo_ui_command cmd = line(u);
        fo_wipe(u->line, sizeof(u->line)); u->length = 0; u->invalid = false; return cmd;
    }
    if (u->invalid) return UI_NONE;
    if (c == 8 || c == 127) { if (u->length) u->line[--u->length] = 0; return UI_NONE; }
    if (c < 32 || u->length >= sizeof(u->line)-1) { u->invalid = true; return UI_NONE; }
    u->line[u->length++] = (char)c; u->line[u->length] = 0; return UI_NONE;
}
