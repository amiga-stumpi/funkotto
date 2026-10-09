#ifndef FO_RAW_WIRE_H
#define FO_RAW_WIRE_H
#include "funkotto/ethernet.h"
#define FO_RAW_HEADER 16u
#define FO_RAW_PAYLOAD (FO_ETH_MAX + 1u)
#define FO_RAW_SIZE (FO_RAW_HEADER + FO_RAW_PAYLOAD + 4u)
#define FO_RAW_ENCODED (FO_RAW_SIZE + FO_RAW_SIZE / 254u + 2u)
enum fo_raw_type { RAW_HELLO = 1, RAW_STATS, RAW_TX, RAW_RX };
uint32_t fo_crc32(const uint8_t *p, size_t n);
size_t fo_cobs_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
size_t fo_cobs_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap);
uint32_t fo_get32(const uint8_t *p);
void fo_put32(uint8_t *p, uint32_t v);
size_t fo_raw_packet(uint8_t *out, uint8_t type, uint64_t session, uint32_t seq, const uint8_t *payload, size_t n);
struct fo_raw {
    uint64_t session; uint32_t sequence, ticket; bool have_request, pending;
    uint8_t request[FO_RAW_SIZE], response[FO_RAW_SIZE]; size_t request_len, response_len;
    uint8_t encoded[FO_RAW_ENCODED], decoded[FO_RAW_SIZE]; size_t used; bool overflow;
    uint32_t bad_frames, replays, sequence_errors;
    /* Core 0 owns this workspace; keep packet-sized objects off its 4 KiB stack. */
    uint8_t payload[FO_RAW_PAYLOAD]; struct fo_frame rx_frame;
};
void fo_raw_start(struct fo_raw *r, uint64_t session);
/* Return a complete encoded response (without delimiters), or zero. */
size_t fo_raw_feed(struct fo_raw *r, uint8_t byte, uint8_t *out);
size_t fo_raw_poll(struct fo_raw *r, uint8_t *out);
#endif
