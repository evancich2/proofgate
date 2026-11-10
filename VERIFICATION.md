# Verification

proofgate uses [CBMC](https://github.com/diffblue/cbmc), a bounded model checker
for C. Results below were produced with CBMC 6.6.0 on x86-64 Linux.

## What each proof checks

`verify/check.sh` runs each harness in `verify/` twice.

1. **Proof run.** CBMC must report no failed property. Every run enables bounds,
   pointer, memory-leak, division-by-zero, signed and unsigned overflow,
   pointer-overflow, conversion and undefined-shift checks. Unwinding assertions
   are on, so a loop that exceeds its bound fails the proof instead of being
   silently cut off.
2. **Vacuity run.** Each `PG_REACH` outcome becomes an assertion that the outcome
   never happens, and CBMC must refute all of them. This shows that the
   harness's assumptions still allow the interesting behaviour.

## Results

"Properties" counts all CBMC checks that passed, including the many from the
C library model. "Functional" counts the `PG_PROVE` assertions in the harness.

| Harness | Result | Properties (functional) | Scope |
| --- | --- | --- | --- |
| `modbus_encode` | Pass | 1731 (8) | Complete: no input-dependent loops |
| `modbus_decode` | Pass | 1739 (7) | Complete: every reply up to the 260-byte maximum |
| `mqtt_remaining` | Pass | 1718 (2) | Complete: the field is at most four bytes |
| `mqtt_publish` | Pass | 1822 (7) | Bounded packet size |
| `api_route` | Pass | 1721 (4) | Bounded path and query lengths |
| `http_too_long` | Pass | 1728 (1) | Scan limit, with a reduced `HTTP_MAX_HEAD` |
| `tlv8_find` | Pass | 1719 (1) | Bounded: short items |
| `http_parse` | **Proof pass, vacuity check not finished** | 1955 (15) | Inputs up to 24 bytes. The vacuity run timed out, so reachability is unconfirmed. At 40 bytes the proof ran out of memory under an 8 GB limit |
| `tlv8_fragments` | **Not finished** | | A full 255-byte fragment and a continuation. Timed out after 15 minutes |
| `api_render` | **Not finished** | | Up to 8 registers, capacity up to 128 bytes. Timed out after 10 minutes; the full 125-register version ran out of memory |

"Not finished" means CBMC gave no verdict, so those harnesses prove nothing yet.
No run has produced a counterexample. Their target properties are listed below
and marked as unfinished.

## Main functional properties

- **HTTP (vacuity check unfinished, except for the size limit):** accepted requests have their head inside the input and ending in
  CRLFCRLF. Target, path, query and headers stay inside the head. Header count
  and content length stay within limits. Oversized input is refused rather than
  waited on forever.
- **Routing:** unit IDs are legal, only read functions are produced, counts are
  in range, and a read range stays inside the address space.
- **Rendering (unfinished):** output never exceeds capacity.
- **Modbus:** encoded requests match the protocol fields byte for byte. Decoded
  replies match the requested transaction, unit and function, frame length
  matches quantity, and registers are the big-endian payload.
- **MQTT:** topic and payload stay inside the packet, QoS is legal, a packet ID
  is present exactly when QoS is non-zero, and topics contain no wildcard or NUL.
- **TLV8:** output stays within capacity, and overruns and small output buffers
  are refused. Fragment reassembly is unfinished.

## Limits and trust assumptions

- `src/main.c` (sockets, timers, connection state) is **not verified**. It is
  covered only by unit-level sanitizer runs of the codecs and the end-to-end test.
- Bounded proofs say nothing directly about inputs beyond their bound. The HTTP
  head-size check is proved only with a reduced limit, while the server accepts
  heads up to 8192 bytes. Closing this is the first item in
  [ROADMAP.md](ROADMAP.md).
- The proofs check the C source. They trust CBMC, its C model and SAT solver,
  and the compiler and libc that build the binary.
- Functional properties are only those written in the harnesses. They are not a
  complete specification of HTTP, Modbus, MQTT or TLV8.
