#ifndef FO_M2_H
#define FO_M2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* M2 bring-up profile: one fixed-size DMA block per direction, zero padding.
   This is not the eventual high-throughput Ethernet transport. */
#define FO_M2_BLOCK 1536u
#define FO_M2_PAYLOAD 1518u
#define FO_M2_ECHO_MAX 1500u
#define FO_M2_VERSION 1u
enum { FO_M2_HELLO=1, FO_M2_INFO=2, FO_M2_ECHO=3, FO_M2_RESET=4 };
enum { FO_M2_OK=0, FO_M2_SESSION=1, FO_M2_SEQUENCE=2, FO_M2_UNSUPPORTED=3,
       FO_M2_LENGTH=4 };
struct fo_m2_frame { uint8_t op; uint16_t sequence, length; const uint8_t *payload; };
struct fo_m2_server {
    uint32_t session, generation, accepted, replays, invalid, sequence_errors;
    uint16_t next;
    bool active, cached;
    uint8_t last[FO_M2_BLOCK], reply[FO_M2_BLOCK];
};
uint16_t fo_m2_crc(const uint8_t *data, size_t length);
uint32_t fo_m2_u32(const uint8_t *p);
void fo_m2_put32(uint8_t *p, uint32_t n);
bool fo_m2_pack(uint8_t *block, uint8_t op, uint16_t sequence,
                const uint8_t *payload, uint16_t length);
bool fo_m2_unpack(const uint8_t *block, struct fo_m2_frame *frame);
void fo_m2_init(struct fo_m2_server *s, uint32_t seed);
void fo_m2_abort(struct fo_m2_server *s);
/* False means no reply: bad wire frame or a non-HELLO before a session. */
bool fo_m2_request(struct fo_m2_server *s, const uint8_t *request, uint8_t *reply);
#endif
