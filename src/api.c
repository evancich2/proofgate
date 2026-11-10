#include "proofgate/api.h"

#include <stdbool.h>

#include "proofgate/modbus.h"

/* Consumes lit from the front of *s if it is there. */
static bool eat(pg_slice *s, const char *lit)
{
    size_t n = 0;
    while (lit[n] != '\0') {
        if (n >= s->len || s->ptr[n] != (uint8_t)lit[n]) {
            return false;
        }
        n++;
    }
    s->ptr += n;
    s->len -= n;
    return true;
}

/* Consumes one to five decimal digits, refusing any value above max. */
static bool eat_number(pg_slice *s, uint32_t max, uint32_t *out)
{
    uint32_t acc = 0;
    size_t n = 0;
    while (n < s->len && s->ptr[n] >= '0' && s->ptr[n] <= '9') {
        if (n == 5u) {
            return false;
        }
        acc = acc * 10u + (uint32_t)(s->ptr[n] - '0');
        n++;
    }
    if (n == 0u || acc > max) {
        return false;
    }
    s->ptr += n;
    s->len -= n;
    *out = acc;
    return true;
}

api_status api_parse_route(pg_slice path, pg_slice query, api_route *route)
{
    pg_slice p = path;
    uint32_t unit = 0;
    uint32_t addr = 0;
    uint32_t count = 1;

    if (eat(&p, "/healthz")) {
        if (p.len != 0u || query.len != 0u) {
            return API_NOT_FOUND;
        }
        route->kind = API_ROUTE_HEALTH;
        route->unit = 0;
        route->fc = 0;
        route->addr = 0;
        route->count = 0;
        return API_OK;
    }

    if (!eat(&p, "/api/v1/modbus/")) {
        return API_NOT_FOUND;
    }
    if (!eat_number(&p, 247u, &unit) || !eat(&p, "/")) {
        return API_BAD_REQUEST;
    }

    uint8_t fc;
    if (eat(&p, "holding/")) {
        fc = MB_FC_READ_HOLDING;
    } else if (eat(&p, "input/")) {
        fc = MB_FC_READ_INPUT;
    } else {
        return API_NOT_FOUND;
    }

    if (!eat_number(&p, 65535u, &addr) || p.len != 0u) {
        return API_BAD_REQUEST;
    }

    if (query.len != 0u) {
        pg_slice q = query;
        if (!eat(&q, "count=") || !eat_number(&q, MB_MAX_REGS, &count) || q.len != 0u) {
            return API_BAD_REQUEST;
        }
    }
    if (count == 0u || addr + count > 65536u) {
        return API_BAD_REQUEST;
    }

    route->kind = API_ROUTE_MODBUS_READ;
    route->unit = (uint8_t)unit;
    route->fc = fc;
    route->addr = (uint16_t)addr;
    route->count = (uint16_t)count;
    return API_OK;
}

typedef struct {
    uint8_t *out;
    size_t cap;
    size_t len;
    bool failed;
} writer;

static void put_str(writer *w, const char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (w->len == w->cap) {
            w->failed = true;
            return;
        }
        w->out[w->len] = (uint8_t)s[i];
        w->len++;
    }
}

static void put_u32(writer *w, uint32_t v)
{
    uint8_t digits[10];
    size_t n = 0;
    do {
        digits[n] = (uint8_t)('0' + (v % 10u));
        n++;
        v /= 10u;
    } while (v != 0u && n < 10u);

    while (n > 0u) {
        if (w->len == w->cap) {
            w->failed = true;
            return;
        }
        n--;
        w->out[w->len] = digits[n];
        w->len++;
    }
}

size_t api_render_registers(uint8_t *out, size_t cap, const api_route *route,
                            const uint16_t *values)
{
    writer w = { out, cap, 0, false };

    put_str(&w, "{\"unit\":");
    put_u32(&w, route->unit);
    put_str(&w, route->fc == MB_FC_READ_INPUT ? ",\"table\":\"input\",\"address\":"
                                              : ",\"table\":\"holding\",\"address\":");
    put_u32(&w, route->addr);
    put_str(&w, ",\"values\":[");
    for (size_t i = 0; i < route->count && !w.failed; i++) {
        if (i != 0u) {
            put_str(&w, ",");
        }
        put_u32(&w, values[i]);
    }
    put_str(&w, "]}");
    return w.failed ? 0u : w.len;
}
