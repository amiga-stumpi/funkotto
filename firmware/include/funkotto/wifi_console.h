#ifndef FO_WIFI_CONSOLE_H
#define FO_WIFI_CONSOLE_H
#include "funkotto/wifi_model.h"
enum fo_ui_command { UI_NONE, UI_HELP, UI_INFO, UI_STATUS, UI_WIFI_STATUS, UI_SCAN, UI_RESULTS, UI_CONNECT, UI_DISCONNECT, UI_STATS, UI_PROFILE, UI_PROMPT_SSID, UI_PROMPT_KEY, UI_INVALID, UI_CANCEL, UI_NOT_IMPLEMENTED, UI_RAW_ON };
struct fo_wifi_console { char line[256]; size_t length; unsigned phase; bool invalid, skip_lf, hex; struct fo_profile profile; };
void fo_ui_init(struct fo_wifi_console *u);
void fo_ui_cancel(struct fo_wifi_console *u);
enum fo_ui_command fo_ui_feed(struct fo_wifi_console *u, uint8_t c);
#endif
