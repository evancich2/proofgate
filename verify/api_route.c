// cbmc: --unwind 34
//
// Bounded path and query lengths, chosen to admit the longest legal route.
#include "harness.h"
#include "proofgate/api.h"
#include "proofgate/modbus.h"

#define PATH_MAX_LEN 32u
#define QUERY_MAX_LEN 10u

int main(void)
{
    uint8_t path_buf[PATH_MAX_LEN];
    uint8_t query_buf[QUERY_MAX_LEN];
    pg_slice path = { path_buf, nondet_size() };
    pg_slice query = { query_buf, nondet_size() };
    __CPROVER_assume(path.len <= PATH_MAX_LEN && query.len <= QUERY_MAX_LEN);

    api_route r;
    api_status st = api_parse_route(path, query, &r);

    PG_REACH(st == API_OK && r.kind == API_ROUTE_HEALTH, "health route");
    PG_REACH(st == API_OK && r.kind == API_ROUTE_MODBUS_READ && r.count > 1u, "read with count");
    PG_REACH(st == API_BAD_REQUEST, "bad request");
    PG_REACH(st == API_NOT_FOUND, "not found");

    if (st == API_OK && r.kind == API_ROUTE_MODBUS_READ) {
        PG_PROVE(r.unit <= 247u, "unit id legal");
        PG_PROVE(r.fc == MB_FC_READ_HOLDING || r.fc == MB_FC_READ_INPUT, "read-only function");
        PG_PROVE(r.count >= 1u && r.count <= MB_MAX_REGS, "count legal");
        PG_PROVE((uint32_t)r.addr + r.count <= 65536u, "range stays in the address space");
    }
    return 0;
}
