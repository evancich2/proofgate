// cbmc: --unwind 34
//
// Bounded: every legal route and register value for readings of up to 8
// registers, and every capacity up to 128 bytes.
#include "harness.h"
#include "proofgate/api.h"
#include "proofgate/modbus.h"

#define OUT_MAX 128u

int main(void)
{
    uint8_t out[OUT_MAX];
    uint16_t values[MB_MAX_REGS];
    api_route r;
    size_t cap = nondet_size();
    r.kind = API_ROUTE_MODBUS_READ;
    r.unit = nondet_u8();
    r.fc = nondet_u8();
    r.addr = nondet_u16();
    r.count = nondet_u16();
    __CPROVER_assume(cap <= OUT_MAX);
    __CPROVER_assume(r.count >= 1u && r.count <= 8u);

    size_t n = api_render_registers(out, cap, &r, values);

    PG_REACH(n != 0u, "rendered");
    PG_REACH(n == 0u, "capacity refused");
    PG_PROVE(n <= cap, "never reports more than capacity");
    if (cap == OUT_MAX) {
        PG_PROVE(n != 0u, "worst-case 8-register reading fits 128 bytes");
    }
    return 0;
}
