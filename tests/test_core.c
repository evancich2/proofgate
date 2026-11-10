/* Example-based tests and randomized stress for the verified core, run under ASan and UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proofgate/api.h"
#include "proofgate/http.h"
#include "proofgate/modbus.h"
#include "proofgate/mqtt.h"
#include "proofgate/tlv8.h"

static int g_failed;
static int g_checks;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        g_checks++;                                                                   \
        if (!(cond)) {                                                                \
            g_failed++;                                                               \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                             \
    } while (0)

static http_status parse(const char *s, http_request *r)
{
    return http_parse_request((const uint8_t *)s, strlen(s), r);
}

static int slice_is(pg_slice s, const char *lit)
{
    return s.len == strlen(lit) && memcmp(s.ptr, lit, s.len) == 0;
}

static void test_http(void)
{
    http_request r;

    CHECK(parse("GET /a?b=1 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", &r) == HTTP_OK);
    CHECK(r.method == HTTP_M_GET);
    CHECK(slice_is(r.path, "/a"));
    CHECK(slice_is(r.query, "b=1"));
    CHECK(r.nheaders == 2u);
    CHECK(slice_is(r.headers[0].name, "Host"));
    CHECK(slice_is(r.headers[0].value, "x"));
    CHECK(!r.keep_alive);

    CHECK(parse("GET / HTTP/1.0\r\n\r\n", &r) == HTTP_OK);
    CHECK(!r.keep_alive);
    CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n", &r) == HTTP_OK);
    CHECK(r.keep_alive);
    CHECK(parse("GET / HTTP/1.1\r\nHost:   padded   \r\n\r\n", &r) == HTTP_OK);
    CHECK(slice_is(r.headers[0].value, "padded"));

    CHECK(parse("GET / HTTP/1.1\r\nHost: x\r\n", &r) == HTTP_INCOMPLETE);
    CHECK(parse("", &r) == HTTP_INCOMPLETE);

    /* Ambiguity that request smuggling depends on. */
    CHECK(parse("GET / HTTP/1.1\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n",
                &r) == HTTP_BAD);
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: +1\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999999999999\r\n\r\n",
                &r) == HTTP_BAD);
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", &r) ==
          HTTP_UNSUPPORTED);
    CHECK(parse("GET / HTTP/1.1\r\nHost : a\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("GET / HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("GET / HTTP/1.1\r\nX: a\nHost: b\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("GET http://x/ HTTP/1.1\r\nHost: a\r\n\r\n", &r) == HTTP_BAD);
    CHECK(parse("GET / HTTP/2.0\r\nHost: a\r\n\r\n", &r) == HTTP_BAD);

    /* Limits. */
    static uint8_t big[HTTP_MAX_HEAD + 16u];
    memset(big, 'a', sizeof big);
    CHECK(http_parse_request(big, sizeof big, &r) == HTTP_TOO_LARGE);

    char many[2048];
    size_t off = (size_t)snprintf(many, sizeof many, "GET / HTTP/1.1\r\nHost: a\r\n");
    for (unsigned i = 0; i < HTTP_MAX_HEADERS; i++) {
        off += (size_t)snprintf(many + off, sizeof many - off, "X%u: 1\r\n", i);
    }
    snprintf(many + off, sizeof many - off, "\r\n");
    CHECK(parse(many, &r) == HTTP_TOO_LARGE);
}

static void test_modbus(void)
{
    uint8_t req[MB_READ_REQ_LEN];
    CHECK(mb_encode_read(req, sizeof req, 0x1234, 1, 3, 0x10, 2) == MB_OK);
    static const uint8_t want[12] = { 0x12, 0x34, 0, 0, 0, 6, 1, 3, 0, 0x10, 0, 2 };
    CHECK(memcmp(req, want, sizeof want) == 0);
    CHECK(mb_encode_read(req, sizeof req, 1, 1, 3, 0, 126) == MB_INVALID_ARG);
    CHECK(mb_encode_read(req, sizeof req, 1, 1, 3, 65535, 2) == MB_INVALID_ARG);
    CHECK(mb_encode_read(req, sizeof req, 1, 1, 5, 0, 1) == MB_INVALID_ARG);
    CHECK(mb_encode_read(req, 11, 1, 1, 3, 0, 1) == MB_INVALID_ARG);

    uint16_t regs[MB_MAX_REGS];
    uint8_t exc = 0;
    size_t used = 0;
    static const uint8_t ok[] = { 0x12, 0x34, 0, 0, 0, 7, 1, 3, 4, 0xAB, 0xCD, 0x00, 0x01 };
    CHECK(mb_decode_read(ok, sizeof ok, 0x1234, 1, 3, 2, regs, MB_MAX_REGS, &exc, &used) == MB_OK);
    CHECK(regs[0] == 0xABCD && regs[1] == 1 && used == sizeof ok);

    for (size_t n = 0; n < sizeof ok; n++) {
        CHECK(mb_decode_read(ok, n, 0x1234, 1, 3, 2, regs, MB_MAX_REGS, &exc, &used) ==
              MB_INCOMPLETE);
    }
    CHECK(mb_decode_read(ok, sizeof ok, 0x9999, 1, 3, 2, regs, MB_MAX_REGS, &exc, &used) == MB_BAD);
    CHECK(mb_decode_read(ok, sizeof ok, 0x1234, 2, 3, 2, regs, MB_MAX_REGS, &exc, &used) == MB_BAD);
    CHECK(mb_decode_read(ok, sizeof ok, 0x1234, 1, 3, 3, regs, MB_MAX_REGS, &exc, &used) == MB_BAD);

    static const uint8_t ex[] = { 0, 1, 0, 0, 0, 3, 1, 0x83, 2 };
    CHECK(mb_decode_read(ex, sizeof ex, 1, 1, 3, 2, regs, MB_MAX_REGS, &exc, &used) == MB_EXCEPTION);
    CHECK(exc == 2 && used == sizeof ex);
}

static void test_mqtt(void)
{
    uint32_t v = 0;
    size_t n = 0;
    static const uint8_t rl[] = { 0xFF, 0xFF, 0xFF, 0x7F };
    CHECK(mqtt_remaining_length(rl, 4, &v, &n) == MQTT_OK);
    CHECK(v == MQTT_MAX_REMAINING && n == 4u);
    static const uint8_t rl5[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    CHECK(mqtt_remaining_length(rl5, 5, &v, &n) == MQTT_BAD);

    mqtt_publish p;
    static const uint8_t q0[] = { 0x30, 7, 0, 3, 'a', '/', 'b', 'h', 'i' };
    CHECK(mqtt_parse_publish(q0, sizeof q0, &p) == MQTT_OK);
    CHECK(slice_is(p.topic, "a/b") && slice_is(p.payload, "hi") && p.qos == 0u);

    static const uint8_t q1[] = { 0x32, 6, 0, 1, 't', 0x12, 0x34, 'x' };
    CHECK(mqtt_parse_publish(q1, sizeof q1, &p) == MQTT_OK);
    CHECK(p.qos == 1u && p.packet_id == 0x1234 && slice_is(p.payload, "x"));

    static const uint8_t wild[] = { 0x30, 3, 0, 1, '#' };
    CHECK(mqtt_parse_publish(wild, sizeof wild, &p) == MQTT_BAD);
    static const uint8_t qos3[] = { 0x36, 3, 0, 1, 't' };
    CHECK(mqtt_parse_publish(qos3, sizeof qos3, &p) == MQTT_BAD);
    static const uint8_t pid0[] = { 0x32, 5, 0, 1, 't', 0, 0 };
    CHECK(mqtt_parse_publish(pid0, sizeof pid0, &p) == MQTT_BAD);
    CHECK(mqtt_parse_publish(q0, sizeof q0 - 1u, &p) == MQTT_INCOMPLETE);
}

static void test_tlv8(void)
{
    uint8_t out[600];
    size_t n = 0;
    static const uint8_t simple[] = { 1, 2, 'h', 'i', 2, 1, 'x' };
    CHECK(tlv8_find(simple, sizeof simple, 2, out, sizeof out, &n) == TLV8_OK);
    CHECK(n == 1u && out[0] == 'x');
    CHECK(tlv8_find(simple, sizeof simple, 9, out, sizeof out, &n) == TLV8_NOT_FOUND);
    CHECK(tlv8_find(simple, sizeof simple, 1, out, 1, &n) == TLV8_TOO_SMALL);
    static const uint8_t cut[] = { 1, 5, 'h' };
    CHECK(tlv8_find(cut, sizeof cut, 1, out, sizeof out, &n) == TLV8_BAD);

    uint8_t frag[300];
    memset(frag, 'a', sizeof frag);
    frag[0] = 6;
    frag[1] = 255;
    frag[257] = 6;
    frag[258] = 3;
    frag[259] = 'x';
    frag[260] = 'y';
    frag[261] = 'z';
    frag[262] = 7;
    frag[263] = 0;
    CHECK(tlv8_find(frag, 264, 6, out, sizeof out, &n) == TLV8_OK);
    CHECK(n == 258u && out[254] == 'a' && out[255] == 'x' && out[257] == 'z');
}

static api_status route(const char *path, const char *query, api_route *r)
{
    pg_slice p = { (const uint8_t *)path, strlen(path) };
    pg_slice q = { (const uint8_t *)query, strlen(query) };
    return api_parse_route(p, q, r);
}

static void test_api(void)
{
    api_route r;
    CHECK(route("/healthz", "", &r) == API_OK && r.kind == API_ROUTE_HEALTH);
    CHECK(route("/api/v1/modbus/1/holding/100", "count=5", &r) == API_OK);
    CHECK(r.unit == 1 && r.fc == 3 && r.addr == 100 && r.count == 5);
    CHECK(route("/api/v1/modbus/247/input/65535", "", &r) == API_OK);
    CHECK(r.fc == 4 && r.addr == 65535 && r.count == 1);

    CHECK(route("/api/v1/modbus/248/input/0", "", &r) == API_BAD_REQUEST);
    CHECK(route("/api/v1/modbus/1/input/65536", "", &r) == API_BAD_REQUEST);
    CHECK(route("/api/v1/modbus/1/input/65535", "count=2", &r) == API_BAD_REQUEST);
    CHECK(route("/api/v1/modbus/1/input/0", "count=0", &r) == API_BAD_REQUEST);
    CHECK(route("/api/v1/modbus/1/input/0", "count=126", &r) == API_BAD_REQUEST);
    CHECK(route("/api/v1/modbus/1/coil/0", "", &r) == API_NOT_FOUND);
    CHECK(route("/nope", "", &r) == API_NOT_FOUND);

    CHECK(route("/api/v1/modbus/2/holding/9", "count=2", &r) == API_OK);
    uint16_t values[2] = { 7, 65535 };
    uint8_t out[128];
    size_t n = api_render_registers(out, sizeof out, &r, values);
    static const char want[] = "{\"unit\":2,\"table\":\"holding\",\"address\":9,\"values\":[7,65535]}";
    CHECK(n == sizeof want - 1u && memcmp(out, want, n) == 0);
    CHECK(api_render_registers(out, n - 1u, &r, values) == 0u);
}

/* Random and mutated-valid input, looking for sanitizer reports. */
static void stress(void)
{
    static const char *seeds[] = {
        "GET /api/v1/modbus/1/holding/3?count=2 HTTP/1.1\r\nHost: a\r\nContent-Length: 0\r\n\r\n",
        "POST /x HTTP/1.0\r\nConnection: keep-alive\r\n\r\n",
    };
    uint8_t buf[512];
    uint16_t regs[MB_MAX_REGS];
    uint8_t out[300];
    srand(12345);

    for (int iter = 0; iter < 400000; iter++) {
        const char *seed = seeds[iter % 2];
        size_t n = strlen(seed);
        memcpy(buf, seed, n);
        for (int m = rand() % 4; m > 0; m--) {
            buf[(size_t)rand() % n] = (uint8_t)rand();
        }
        size_t cut = (size_t)rand() % (n + 1u);

        /* Copy to an exact-size allocation so ASan sees any overread. */
        uint8_t *exact = malloc(cut + 1u);
        if (exact == NULL) {
            abort();
        }
        memcpy(exact, buf, cut);

        http_request r;
        (void)http_parse_request(exact, cut, &r);
        free(exact);

        size_t rlen = (size_t)rand() % sizeof buf;
        exact = malloc(rlen + 1u);
        if (exact == NULL) {
            abort();
        }
        for (size_t i = 0; i < rlen; i++) {
            exact[i] = (uint8_t)rand();
        }
        mqtt_publish p;
        uint8_t exc;
        size_t used;
        size_t got;
        (void)mqtt_parse_publish(exact, rlen, &p);
        (void)tlv8_find(exact, rlen, exact[0], out, sizeof out, &got);
        (void)mb_decode_read(exact, rlen, 0, 0, 3, 1, regs, MB_MAX_REGS, &exc, &used);
        free(exact);
    }
}

int main(void)
{
    test_http();
    test_modbus();
    test_mqtt();
    test_tlv8();
    test_api();
    stress();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
