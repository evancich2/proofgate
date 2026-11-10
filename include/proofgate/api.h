/* REST routing and JSON rendering. Pure: no allocation, no I/O. */
#ifndef PROOFGATE_API_H
#define PROOFGATE_API_H

#include <stddef.h>
#include <stdint.h>

#include "proofgate/slice.h"

typedef enum {
    API_ROUTE_HEALTH,
    API_ROUTE_MODBUS_READ
} api_route_kind;

typedef enum {
    API_OK = 0,
    API_NOT_FOUND,
    API_BAD_REQUEST
} api_status;

typedef struct {
    api_route_kind kind;
    uint8_t unit;
    uint8_t fc; /* MB_FC_READ_HOLDING or MB_FC_READ_INPUT */
    uint16_t addr;
    uint16_t count;
} api_route;

/*
 * Routes /healthz and /api/v1/modbus/{unit}/{holding|input}/{addr}[?count=N].
 * On API_OK with a Modbus route: unit <= 247, 1 <= count <= 125 and
 * addr + count <= 65536.
 */
api_status api_parse_route(pg_slice path, pg_slice query, api_route *route);

/* Renders a reading as JSON. Returns bytes written, or 0 if cap is too small. */
size_t api_render_registers(uint8_t *out, size_t cap, const api_route *route,
                            const uint16_t *values);

#endif
