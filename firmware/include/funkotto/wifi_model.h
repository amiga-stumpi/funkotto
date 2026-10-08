#ifndef FO_WIFI_MODEL_H
#define FO_WIFI_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct fo_profile { uint8_t ssid[32], key[63]; uint8_t ssid_len, key_len; };
enum fo_wifi_state { FO_INITIALIZING, FO_UNCONFIGURED, FO_DISCONNECTED, FO_CONNECTING, FO_LINK_UP, FO_RETRY_WAIT, FO_ERROR };
enum fo_wifi_error { FO_OK, FO_NO_PROFILE, FO_TIMEOUT, FO_BAD_AUTH, FO_NO_NETWORK, FO_DRIVER_ERROR, FO_LINK_LOST, FO_JOIN_FAILED };
struct fo_wifi_model {
    struct fo_profile profile;
    enum fo_wifi_state state;
    enum fo_wifi_error error;
    bool configured, wanted;
    uint8_t backoff;
    uint32_t epoch, attempts, links;
    uint64_t deadline_ms;
};
void fo_wipe(void *p, size_t n);
bool fo_profile_valid(const struct fo_profile *p);
void fo_model_init(struct fo_wifi_model *m);
bool fo_model_profile(struct fo_wifi_model *m, const struct fo_profile *p);
void fo_model_disconnect(struct fo_wifi_model *m);
bool fo_model_connect(struct fo_wifi_model *m, uint64_t now);
bool fo_model_tick(struct fo_wifi_model *m, uint64_t now);
void fo_model_fail(struct fo_wifi_model *m, enum fo_wifi_error error, uint64_t now);
void fo_model_link_up(struct fo_wifi_model *m, uint32_t epoch);
const char *fo_state_name(enum fo_wifi_state s);
const char *fo_error_name(enum fo_wifi_error e);
#endif
