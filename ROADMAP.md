# Roadmap

Rough effort in hours. Each protocol lands the same way: a pure codec, a CBMC
harness with reachability checks, then socket integration in the unverified
shell.

## Proof depth

- [ ] Replace the bounded HTTP proof with loop contracts or Frama-C WP, so it
      covers the full 8 KB head (12h)
- [ ] Prove `tlv8_next` termination and reassembly for inputs of any length (4h)
- [ ] Move connection state out of `main.c` into a pure, verified state
      machine so the shell only performs syscalls (10h)
- [ ] Prove the HTTP response writer and replace `snprintf` (3h)

## SCADA

- [ ] Modbus coils and discrete inputs, function codes 1 and 2 (3h)
- [ ] Modbus writes, off by default, per-register allow-list (6h)
- [ ] Persistent Modbus connections and multiple devices from a config file (6h)
- [ ] DNP3 link-layer and transport framing with CRC checks (12h)
- [ ] OPC UA binary message framing, read-only (16h)

## Home automation

- [ ] HomeKit Accessory Protocol over IP (MFi-free, for personal use):
  - [ ] mDNS advertisement of `_hap._tcp` (6h)
  - [ ] Pair Setup using SRP-6a (12h)
  - [ ] Pair Verify using Curve25519 and Ed25519 (6h)
  - [ ] ChaCha20-Poly1305 session framing (4h)
  - [ ] Accessory and characteristic database, `/accessories` and
        `/characteristics` (8h)
- [ ] MQTT client session: CONNECT, SUBSCRIBE, keepalive; bridge points to
      topics (8h)
- [ ] Matter and Thread feasibility study (4h)
- [ ] Zigbee via a serial coordinator, starting with ZNP framing (10h)

## Server

- [ ] Authentication with bearer tokens, compared in constant time (4h)
- [ ] TLS through a small, separately verified or well-audited library (8h)
- [ ] Non-blocking upstream calls, so a slow device cannot stall other clients (6h)
- [ ] Metrics endpoint and structured logs (3h)

## Quality

- [ ] libFuzzer and AFL++ targets for every codec (3h)
- [ ] Interoperability tests against real Modbus and MQTT implementations (4h)
- [ ] API documentation and a deployment guide (3h)
