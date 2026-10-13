#ifndef FO_ETHERLINK_H
#define FO_ETHERLINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* FOE1 payload protocol. No GPIO, SDK, OS, allocation or stream framing. */
#define FO_EL_HEADER 24u
#define FO_EL_MTU 1514u
#define FO_EL_PAYLOAD (FO_EL_MTU+1u)
#define FO_EL_PACKET (FO_EL_HEADER+FO_EL_PAYLOAD+4u)
#define FO_EL_SLOTS 8u
#define FO_EL_CONFIG_MAX 128u
enum fo_el_op { EL_HELLO=1, EL_STATUS, EL_TX, EL_TX_RESULT, EL_RX,
    EL_CONFIG, EL_ARM, EL_CLOSE };
/* 0..9 intentionally match the existing Ethernet queue results. */
enum fo_el_result { EL_OK, EL_EMPTY, EL_BUSY, EL_NO_LINK, EL_INVALID,
    EL_ABORTED, EL_DRIVER, EL_PENDING, EL_SESSION, EL_SEQUENCE,
    EL_UNSUPPORTED, EL_EPOCH, EL_STALE, EL_INTERNAL };
enum fo_el_event { EL_RX_READY=1, EL_TX_DONE=2, EL_LINK_CHANGED=4,
    EL_CONFIG_DONE=8, EL_EVENT_OVERFLOW=16 };
enum fo_el_cap { EL_CAP_ETH=1, EL_CAP_ASYNC_TX=2, EL_CAP_EVENTS=4, EL_CAP_CONFIG=8 };
struct fo_el_frame {
    uint8_t op; uint16_t length; uint32_t sequence, epoch;
    uint64_t session; const uint8_t *payload;
};
struct fo_el_status {
    uint8_t mac[6], tx_free, rx_count, tx_done;
    bool link;
    uint32_t epoch, rx_serial, tx_serial, config_serial;
};
/* Backend is exclusively owned by this session. Each callback takes/releases
 * its own queue locks; never hold a cross-core lock over CRC/parsing or I/O.
 * Snapshot serials advance on arrival/drop, TX completion/abort, config job
 * completion. Done TX slots remain occupied until result() consumes them. */
struct fo_el_backend {
    void *ctx;
    void (*enable)(void *ctx, bool enabled); /* flush queues on owner change */
    void (*snapshot)(void *ctx, struct fo_el_status *out);
    /* Epoch comparison and queue operation MUST share the backend lock. */
    uint8_t (*tx)(void *ctx, uint32_t epoch, const uint8_t *p, size_t n, uint32_t *ticket);
    uint8_t (*result)(void *ctx, uint32_t ticket, int32_t *sdk_error);
    uint8_t (*rx)(void *ctx, uint32_t epoch, uint8_t *out, uint16_t *length);
    /* Optional, out capacity 128. Must not expose credentials in replies.
     * Job state belongs to the backend, not the transport replay cache. */
    size_t (*config)(void *ctx, uint8_t op, const uint8_t *p, size_t n, uint8_t *out);
};
struct fo_el_server {
    struct fo_el_backend io;
    struct fo_el_status seen;
    uint64_t generation, session;
    uint32_t sequence, event_generation;
    uint8_t latched_events;
    bool active, cached, armed, irq_requested, seen_valid;
    size_t last_length, reply_length;
    uint8_t last[FO_EL_PACKET], reply[FO_EL_PACKET], payload[FO_EL_PAYLOAD];
};
uint16_t fo_el_u16(const uint8_t *p);
uint32_t fo_el_u32(const uint8_t *p);
uint64_t fo_el_u64(const uint8_t *p);
void fo_el_put16(uint8_t *p, uint16_t v);
void fo_el_put32(uint8_t *p, uint32_t v);
void fo_el_put64(uint8_t *p, uint64_t v);
uint32_t fo_el_crc32(const uint8_t *p, size_t n);
size_t fo_el_pack(uint8_t *out, size_t cap, uint8_t op, uint64_t session,
                  uint32_t sequence, uint32_t epoch, const uint8_t *p, size_t n);
bool fo_el_unpack(const uint8_t *p, size_t n, struct fo_el_frame *out);
/* Caller supplies a fresh nonzero boot seed, exclusive ownership, and static
 * server storage (~5 KiB). No traffic until a validated HELLO. */
void fo_el_init(struct fo_el_server *s, const struct fo_el_backend *io, uint64_t seed);
void fo_el_reset(struct fo_el_server *s); /* owner loss: flush/wipe; retain generation */
void fo_el_poll(struct fo_el_server *s);
bool fo_el_take_irq(struct fo_el_server *s); /* one notification per successful ARM */
/* One complete packet each way. Distinct non-overlapping buffers; out capacity
 * FO_EL_PACKET. 0 = corrupt/truncated packet, no reply and no operation. */
size_t fo_el_request(struct fo_el_server *s, const uint8_t *in, size_t n,
                     uint8_t out[FO_EL_PACKET]);
#endif
