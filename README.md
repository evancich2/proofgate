# proofgate

A small HTTP gateway for industrial and home-automation devices, written in
plain C11, whose parsers for untrusted network input are checked with the CBMC
bounded model checker.

proofgate exposes Modbus TCP registers as a read-only JSON REST API. It is for
people who run PLCs, sensors or home-automation gear and want a simple HTTP view
of those devices, without putting a large, unaudited parser on the network path.

**Status: early and new.** The project started in November 2026 and has no
releases or known users yet. It has not been tested against physical devices,
only a simulated Modbus server. Do not put it in front of equipment where a
failure matters.

## What works today

| Area | State |
| --- | --- |
| HTTP/1.1 server (GET only, pipelining, timeouts, 64 connections) | Working and tested. Head-size limit proved, full parser proof not finished |
| REST routing | Working, proved for bounded inputs |
| JSON rendering | Working and tested, proof not finished |
| Modbus TCP read holding and input registers (FC 3 and 4) | Working, codec proved |
| MQTT 3.1.1 PUBLISH decoder | Codec only, proved for bounded packets, not connected to the server yet |
| HomeKit TLV8 codec | Codec only, not connected to the server yet. Short items proved, fragment reassembly proof not finished |
| HomeKit pairing, sessions and accessories | Not started, see [ROADMAP.md](ROADMAP.md) |
| DNP3, OPC UA, Zigbee, Matter | Not started, see [ROADMAP.md](ROADMAP.md) |
| TLS and authentication | Not started. Keep the default loopback bind. |

## What is verified, and what is not

The pure codecs (`src/http.c`, `src/api.c`, `src/modbus.c`, `src/mqtt.c`,
`src/tlv8.c`) do no allocation and no I/O. CBMC harnesses for them check memory
safety, absence of undefined behaviour and arithmetic overflow, and functional
properties, with a reachability check so that no proof passes vacuously.

**Seven of the ten proofs pass. Three do not finish yet within the limits
used:** the full HTTP parser, JSON rendering and TLV8 fragment reassembly. None
has found a defect, but none counts as proved.

`src/main.c`, which owns sockets, timers and connection state, is ordinary
unverified C. Some passing proofs are bounded to short inputs. **proofgate is not a
fully verified web server.** [VERIFICATION.md](VERIFICATION.md) lists each
proof, its bound and the remaining trust assumptions.

## Build and run

You need a C11 compiler and `make` on Linux or another POSIX system. The tests
also need `curl`, and the proofs need [CBMC](https://github.com/diffblue/cbmc).

```sh
make                 # builds build/proofgate
make test            # unit tests under ASan and UBSan
make e2e             # the real server against a simulated Modbus device
make verify          # every CBMC proof; three need more memory and time than most machines have
make proof-tlv8_find # one proof
```

Run it against a Modbus TCP device:

```sh
./build/proofgate --bind 127.0.0.1 --port 8080 --modbus 192.0.2.10:502
```

`--bind` defaults to `127.0.0.1` and `--port` to `8080`.

## API

| Request | Result |
| --- | --- |
| `GET /healthz` | `{"status":"ok"}` |
| `GET /api/v1/modbus/{unit}/{holding\|input}/{addr}[?count=N]` | Register values |

`unit` is 0 to 247, `count` is 1 to 125, and `addr + count` must not exceed
65536.

```console
$ curl 'http://127.0.0.1:8080/api/v1/modbus/1/input/2?count=3'
{"unit":1,"table":"input","address":2,"values":[6,9,12]}
```

| Status | Meaning |
| --- | --- |
| 400 | Invalid route parameters |
| 404 | Unknown route |
| 405 | Method other than GET |
| 413 | Request carried a body |
| 501 | `Transfer-Encoding` is not supported |
| 502 | Modbus exception, or the device is unreachable |
| 504 | The device did not answer in time |

## Layout

```text
include/proofgate/  Codec interfaces
src/                Verified codecs, plus the unverified shell in main.c
tests/              Unit tests, simulated Modbus device, end-to-end script
verify/             CBMC harnesses and the proof runner
```

## Plans

[ROADMAP.md](ROADMAP.md) lists the planned work with rough estimates, including
deeper proofs, more of Modbus, MQTT sessions and HomeKit Accessory Protocol.

## License

[MIT](LICENSE)
