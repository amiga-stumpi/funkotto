# FunkOtto W3 USB raw protocol v1

Development/test protocol, not the later parallel-port ABI. One CDC interface;
ASCII `raw on\n` explicitly enters binary mode. DTR low exits, flushes the
packet queues and restores the console without clearing the WLAN profile.
SDK and console stdio output is disabled during binary mode. Binary I/O uses
the SDK USB driver's mutex-protected byte functions, bypassing CRLF conversion.

Each message is `00 COBS(decoded-message) 00`. Repeated zero delimiters are
ignored. Encoded messages are bounded to 1543 bytes, decoded to 1535 bytes.
Oversize/bad COBS/header/CRC messages are discarded without side effects.
Both ends resynchronize at the next delimiter. No unsolicited binary messages.

All multibyte fields use network byte order; no packed C struct is transmitted.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | Version = 1 |
| 1 | 1 | Type; response has bit 7 set |
| 2 | 2 | Payload length, max 1515 |
| 4 | 4 | Sequence, starts at 1; never wraps in a session |
| 8 | 8 | Random nonzero session ID |
| 16 | length | Payload |
| 16 + length | 4 | CRC-32/ISO-HDLC over header + payload; same as Python zlib.crc32 |

The Pico generates a fresh session ID when `raw on` is processed. First request
is HELLO, seq=1, session=0, empty payload. Response contains the real session ID.
Every subsequent request uses that ID and the next sequence number. Host allows
one outstanding request. Same sequence and **identical complete request bytes**
replay the cached response; a pending TX replay waits for the same completion.
Same sequence with altered content, an old/future sequence or a different request
while TX is pending returns SEQUENCE without advancing state. Wrong session
returns SESSION. Sequence 0 is invalid. Reopen the session before sequence wraps.

A missing/truncated response is retried with exactly the same request. The client
uses four attempts, two seconds each by default. After final timeout or changed
session, report unknown TX outcome and close the session; never blindly submit
that packet as a new request. SDK writes have a 10 ms no-progress limit; missing
USB bytes are detected by framing/CRC and recovered through response replay.

| Request type | Request payload | Response payload |
| --- | --- | --- |
| 1 HELLO | empty | status u8, station MAC 6 bytes, link u8, max frame u16=1514, slot count u8=8, epoch u32 |
| 2 STATS | empty | status u8, link u8, TX used u8, RX used u8, epoch u32, 16 counters u32 |
| 3 TX | Ethernet frame, 14..1514 bytes, no FCS | status u8, signed SDK error i32; response after SDK returns or request aborts |
| 4 RX | empty | status u8, then Ethernet frame only when OK |

Status values: 0 OK, 1 EMPTY, 2 BUSY, 3 NO_LINK, 4 INVALID, 5 ABORTED,
6 DRIVER, 7 PENDING (internal only), 8 SESSION, 9 SEQUENCE. Protocol errors use
a one-byte status payload. Unknown commands/payload lengths return INVALID.

STATS counter order: tx_queued, tx_ok, tx_error, tx_aborted, tx_busy, tx_invalid,
rx_queued, rx_full, rx_inactive, rx_invalid, rx_flushed, tx_high_water,
rx_high_water, bad_frames, replays, sequence_errors. All counters wrap modulo
2^32. Ethernet counters persist for the boot; protocol counters reset per session.
Link epoch comes from the WLAN state machine. The test client rejects a completed
run if its final epoch differs from HELLO, and a new run is required after loss.

## Packet and ownership contract

- Ethernet II only; EtherType >= 0x0600. VLAN 0x8100/0x88a8 and EAPOL 0x888e
  are rejected; CYW43 owns authentication. No VLAN/802.3 LLC/FCS support.
- TX source MAC must equal the station MAC. Destination may be unicast,
  multicast or broadcast. Short frames are zero-padded to 60 bytes.
- Eight TX and eight RX slots each own a 1600-byte buffer; accepted wire frame
  maximum remains 1514. No per-packet allocation.
- Core 0 submits copies; Core 1 claims the oldest TX and copies to its private
  staging buffer. The cross-core critical section is released before the SDK
  call. The pinned CYW43 `cyw43_ll_send_ethernet` copies into `spid_buf` before
  returning; `is_pbuf=false`. Staging storage is reusable after return.
- RX callback checks interface, pointer/type/length and copies into owned memory
  before returning. It performs no SDK call, logging, sleep or flash operation.
  Only a short memory-copy critical section protects the cross-core queues.
- TX completion remains in its slot until consumed, so completion records cannot
  overflow independently. SDK success means accepted, not delivered/acknowledged
  by the peer. The peer's matching ICMP reply supplies end-to-end evidence.
- RX full drops the new frame, increments rx_full, never overwrites an older one.
  Frames arriving outside an active session/link are counted and discarded.
- Link down/epoch change flushes RX and marks queued/inflight TX ABORTED. No old
  queued packet is automatically retransmitted after reconnect. A packet already
  handed to CYW43 cannot be recalled. Late SDK completion cannot overwrite an
  aborted/reused slot. New USB sessions discard all old queue/completion state.
- Replaying a cached successful RX/TX response reports the original operation's
  result, even if the link has since changed; it does not dequeue/transmit again.
  Check link epoch separately, and start a new test after a link change.
- USB is stop-and-wait and RX is host-pulled. This protocol measures functionality,
  not maximum radio throughput or the future Amiga parallel-port performance.

The pinned CYW43 raw-send implementation retains a link-time reference to
`pbuf_copy_partial` even with `CYW43_LWIP=0`. `cyw43_no_lwip.c` supplies a fatal
guard for this unreachable branch; it does not emulate pbuf or add lwIP. An
accidental `is_pbuf=true` call stops the service and the watchdog resets it.
Core 0 retains the SDK's 4 KiB scratch-bank stack; packet-sized protocol
workspaces are static. Core 1 uses the existing explicit 8 KiB SRAM stack.
