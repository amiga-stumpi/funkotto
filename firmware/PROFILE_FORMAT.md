# W4 profile journal, schema 1

Two fixed 4096-byte sectors on the Pico 2 W's 4 MiB flash:

| Slot | Flash offset | XIP address |
| --- | --- | --- |
| A | 0x003fe000 | 0x103fe000 |
| B | 0x003ff000 | 0x103ff000 |

Application ends below 0x103fd000; RP2350-E10 has its own sector at
0x103fd000..0x103fe000. UF2 validation prohibits application writes into both
areas except the explicitly checked E10 compatibility block at 0x103fdf00.
Only W4 includes the journal writer. No filesystem, dynamic packet allocation,
encryption or flash write on ordinary connection/retry.

## Stable envelope

Each sector contains a 256-byte data page and a separate 256-byte commit page;
the remaining 3584 bytes must be 0xff. Multibyte fields use big endian.
CRC32 uses the ISO-HDLC polynomial, initial/final XOR all ones.

| Data page offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII FOP4 |
| 4 | 2 | Payload schema = 1 |
| 6 | 1 | Kind: 1 profile, 2 delete tombstone |
| 7 | 1 | Auth: 1 WPA2 AES PSK; tombstone 0 |
| 8 | 4 | Sequence modulo 2^32 |
| 12 | 2 | Country DE |
| 14 | 1 | SSID length 1..32; tombstone 0 |
| 15 | 1 | Passphrase length 8..63 printable ASCII; tombstone 0 |
| 16 | 32 | SSID bytes, unused bytes 0xff; embedded zero allowed |
| 48 | 63 | Passphrase bytes, unused bytes 0xff |
| 111 | 141 | 0xff |
| 252 | 4 | CRC32 of data bytes 0..251 |

For tombstones bytes 16..251 are all 0xff, with no credentials.
Commit page: ASCII FOCM at 0, sequence at 4, data-page CRC at 8, bitwise inverse
of sequence at 12, bytes 16..255 all 0xff. The meaningful commit occupies its
first 16 bytes; no second programming of the data page is needed. Commit page
is only programmed after full erase verification and data-page readback.

The envelope remains fixed for future payload schemas. An intact envelope
with unsupported schema/kind/auth/country can participate in sequence selection
but is never used as credentials. If newest, it blocks autoconnect and normal
save until explicit erase. Invalid CRC/commit/length/padding is ignored;
fall back to the other valid slot. Neither valid: EMPTY if both sectors are
all 0xff, otherwise CORRUPT, with no credentials or automatic connection.

For two valid records, A is newer than B iff unsigned (A.seq-B.seq) is in
1..0x7fffffff. Equal or exactly half-range-separated sequences are AMBIGUOUS,
fail closed and block normal save. A tombstone is a record and participates in
the same ordering. This handles normal sequence wrap without preferring stale data.

## Save and delete

Save uses the other sector and next sequence: erase, verify all 0xff, program
data, compare, program commit, compare, reload and select the new record.
The previous valid slot is retained as fallback. Saving identical logical
credentials performs no erase/program. Current service still quiesces WLAN
for the command. Caller profile is private model RAM and must not alias the
journal cache, which is wiped and reloaded during operations.

Delete first commits a newer credential-free tombstone in the other sector,
then erases/verifies the old sector. Before the tombstone becomes valid, an
interrupted operation may retain the old profile; do not report success yet.
Once tombstone is valid, an interrupted old-sector erase must never reactivate
that profile. Boot reloads the tombstone, retries old-sector cleanup and stays
unconfigured even if cleanup fails. It does not erase the last tombstone.
A later explicit save may supersede it.

Explicit delete can also recover incompatible/ambiguous records. For ambiguous
selection overwrite slot A with a tombstone immediately newer than slot B's
intact envelope, then clean B. For no intact envelope use sequence 1. This is
an intentional user erase, never an automatic boot-time format/migration.
Success is emitted only after readback and old-secret cleanup. Failed SDK exit
may mean a write already completed; reload current contents, retain an error
and do not claim success. Repeating save/delete is safe and bounded.

## Pico flash exclusion

Core 0 calls `flash_safe_execute_core_init` before launching Core 1. The pinned
SDK launch temporarily disables the FIFO IRQ during its boot handshake. Core 1
also initializes flash safety before storage work. The SDK multicore-lockout
backend is explicitly enabled; no ASSUME_CORE_SAFE shortcut is used.

Only Core 1 writes. It disconnects the WLAN model, deinitializes CYW43, closes
raw packet queues, then opens the flash window with no cross-core lock held.
Every operation checks locked Amiga pins, initialized Core-0 victim and **all
DMA channels neither claimed nor busy**. Core 0 has no DMA user in this stage.
Unknown DMA activity rejects the request instead of aborting another peripheral.

`flash_safe_execute` parks Core 0 in the SDK SRAM handler and disables local
interrupts before the callback. Entry/exit timeout is 1000 ms; callback is not
called after failed entry. The RAM callback uses only SDK RAM/ROM erase/program
functions and aligned SRAM data buffers, never USB/CYW43/logging. Each operation
releases the exclusion before readback. The existing hardware watchdog remains
active; no unbounded watchdog-feed loop is added. Real erase/program duration,
USB pause and reset behavior still need hardware verification.

At boot only journal reads and, when a valid tombstone requires it, cleanup run
before driver initialization. A valid profile is copied into the private WLAN
model and starts the existing retry state machine without waiting for USB.
Invalid/unsupported storage has no automatic write, no profile and no boot loop.
The public status contains SSID, flags, sequence and errors, never passphrase.
`stored` compares RAM with committed profile; `flash_profile` reports existence.

## Verification boundary

Host tests model interruption at every byte boundary of NOR erase/program for
save and delete, then recreate boot state. This is not a physical brownout model
or proof of all real flash failure modes. CRC32 is corruption detection, not
authentication. Deliberate mutation, physical flash damage and forensic secret
recovery are outside this journal's guarantees. Real cold starts, interrupted
operations and profile-preserving UF2 update remain hardware acceptance items.
