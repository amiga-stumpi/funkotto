#ifndef FO_CONFIG_PROTOCOL_H
#define FO_CONFIG_PROTOCOL_H
#include "funkotto/wifi_service.h"
#define FO_CFG_MAX 128u
#define FO_CFG_CAPS UINT32_C(0x3f)
enum fo_cfg_op { CFG_INFO=1, CFG_STATUS, CFG_SCAN, CFG_SCAN_GET, CFG_SET,
    CFG_CONNECT, CFG_DISCONNECT, CFG_SAVE, CFG_ERASE, CFG_JOB };
enum fo_cfg_result { CFG_OK, CFG_QUEUED, CFG_BUSY, CFG_INVALID, CFG_UNSUPPORTED,
    CFG_STALE, CFG_EMPTY, CFG_SESSION, CFG_SEQUENCE };
/* The transport owns one instance per exclusive session. No SDK dependency.
 * Single Core-0 caller; fo_wifi_* supplies the existing synchronized backend. */
struct fo_config {
    struct fo_wifi_status snapshot;
    uint32_t job, baseline;
    uint8_t job_op, job_reply;
    bool pending;
};
void fo_config_init(struct fo_config *c);
/* out must have FO_CFG_MAX bytes. All replies start with fo_cfg_result.
 * Only the most recent accepted job is retained. Call before servicing another
 * transport; the owner must exclude other fo_wifi_submit callers. */
size_t fo_config_exec(struct fo_config *c, uint8_t op, const uint8_t *p,
                      size_t n, uint8_t out[FO_CFG_MAX]);
#endif
