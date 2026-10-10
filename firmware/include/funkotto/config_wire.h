#ifndef FO_CONFIG_WIRE_H
#define FO_CONFIG_WIRE_H
#include "funkotto/config_protocol.h"
#define FO_CFG_WIRE_VERSION 2u
#define FO_CFG_SIZE (20u+FO_CFG_MAX)
#define FO_CFG_ENCODED (FO_CFG_SIZE+FO_CFG_SIZE/254u+2u)
struct fo_config_wire {
    struct fo_config config;
    uint64_t session;
    uint32_t sequence;
    bool have_request, overflow;
    size_t used, request_len, response_len;
    /* Exact last request retained for replay; may contain a key until the next
     * accepted request or close. All transient buffers are wiped after parsing. */
    uint8_t request[FO_CFG_SIZE], response[FO_CFG_SIZE];
    uint8_t input[FO_CFG_ENCODED], decoded[FO_CFG_SIZE], payload[FO_CFG_MAX];
};
void fo_config_wire_start(struct fo_config_wire *w, uint64_t session);
void fo_config_wire_close(struct fo_config_wire *w);
void fo_config_wire_expire_partial(struct fo_config_wire *w);
size_t fo_config_wire_feed(struct fo_config_wire *w, uint8_t byte, uint8_t out[FO_CFG_ENCODED]);
#endif
