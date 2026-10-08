#include <string.h>
#include "funkotto/wifi_model.h"
void fo_wipe(void *p, size_t n) { volatile uint8_t *v = p; while (n--) *v++ = 0; }
bool fo_profile_valid(const struct fo_profile *p) {
    if (!p->ssid_len || p->ssid_len > 32 || p->key_len < 8 || p->key_len > 63) return false;
    for (unsigned i = 0; i < p->key_len; ++i) if (p->key[i] < 32 || p->key[i] > 126) return false;
    return true;
}
void fo_model_init(struct fo_wifi_model *m) { memset(m, 0, sizeof(*m)); m->state = FO_UNCONFIGURED; }
void fo_model_disconnect(struct fo_wifi_model *m) {
    m->wanted = false; ++m->epoch; m->backoff = 0; m->error = FO_OK;
    m->state = m->configured ? FO_DISCONNECTED : FO_UNCONFIGURED;
}
bool fo_model_profile(struct fo_wifi_model *m, const struct fo_profile *p) {
    if (!fo_profile_valid(p)) return false;
    fo_model_disconnect(m); fo_wipe(&m->profile, sizeof(m->profile));
    m->profile = *p; m->configured = true; m->state = FO_DISCONNECTED; return true;
}
bool fo_model_connect(struct fo_wifi_model *m, uint64_t now) {
    if (!m->configured) { m->error = FO_NO_PROFILE; return false; }
    if (m->wanted && (m->state == FO_CONNECTING || m->state == FO_LINK_UP)) return true;
    m->wanted = true; m->backoff = 0; m->state = FO_RETRY_WAIT; m->deadline_ms = now; return true;
}
void fo_model_fail(struct fo_wifi_model *m, enum fo_wifi_error error, uint64_t now) {
    m->error = error;
    if (!m->wanted) return;
    const uint32_t wait = m->backoff < 5 ? (UINT32_C(1000) << m->backoff) : 30000u;
    if (m->backoff < 5) ++m->backoff;
    m->state = FO_RETRY_WAIT; m->deadline_ms = now + wait; ++m->epoch;
}
bool fo_model_tick(struct fo_wifi_model *m, uint64_t now) {
    if (!m->wanted) return false;
    if (m->state == FO_CONNECTING && now >= m->deadline_ms) fo_model_fail(m, FO_TIMEOUT, now);
    if (m->state != FO_RETRY_WAIT || now < m->deadline_ms) return false;
    m->state = FO_CONNECTING; m->error = FO_OK; m->deadline_ms = now + 15000;
    ++m->attempts; ++m->epoch; return true;
}
void fo_model_link_up(struct fo_wifi_model *m, uint32_t epoch) {
    if (!m->wanted || m->state != FO_CONNECTING || epoch != m->epoch) return;
    m->state = FO_LINK_UP; m->backoff = 0; m->error = FO_OK; ++m->links;
}
const char *fo_state_name(enum fo_wifi_state s) {
    static const char *const names[] = {"INITIALIZING", "UNCONFIGURED", "DISCONNECTED", "CONNECTING", "LINK_UP", "RETRY_WAIT", "ERROR"};
    return (unsigned)s < sizeof(names)/sizeof(names[0]) ? names[s] : "INVALID";
}
const char *fo_error_name(enum fo_wifi_error e) {
    static const char *const names[] = {"OK", "NO_PROFILE", "TIMEOUT", "BADAUTH", "NONET", "DRIVER", "LINK_LOST", "JOIN_FAILED"};
    return (unsigned)e < sizeof(names)/sizeof(names[0]) ? names[e] : "INVALID";
}
