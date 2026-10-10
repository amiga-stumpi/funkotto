#ifndef FO_AMIGA_CONFIG_CLIENT_H
#define FO_AMIGA_CONFIG_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#include "funkotto/config_protocol.h"
/* One exchange returns a complete FOC1 payload, or zero on transport failure.
 * Neither this interface nor the client knows USB, CIA registers or framing. */
struct cfg_transport {
    size_t (*exchange)(uint8_t op, const uint8_t *p, size_t n, uint8_t *out);
    int (*wait)(void); /* paced wait; zero requests cancellation */
};
enum cfg_client_result { CC_OK=0, CC_TRANSPORT=-1, CC_FORMAT=-2,
    CC_TIMEOUT=-3, CC_CANCELLED=-4, CC_REMOTE=-5 };
struct cfg_client {
    struct cfg_transport transport;
    uint8_t response[FO_CFG_MAX];
    size_t length;
    uint8_t remote_result, job_reply;
};
uint32_t cfg_u32(const uint8_t *p);
uint16_t cfg_u16(const uint8_t *p);
void cfg_put32(uint8_t *p, uint32_t v);
int cfg_query(struct cfg_client *c, uint8_t op, const uint8_t *p, size_t n);
int cfg_mutate(struct cfg_client *c, uint8_t op, const uint8_t *p, size_t n);
int cfg_scan(struct cfg_client *c);
int cfg_wait_link(struct cfg_client *c);
#endif
