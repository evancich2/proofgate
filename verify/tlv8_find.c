// cbmc: --unwind 22
//
// Bounded: short items. Fragment reassembly is covered by tlv8_fragments.c.
#include "harness.h"
#include "proofgate/tlv8.h"

#define INPUT_MAX 20u

int main(void)
{
    uint8_t buf[INPUT_MAX];
    uint8_t out[INPUT_MAX];
    size_t len = nondet_size();
    size_t cap = nondet_size();
    size_t out_len = 0;
    uint8_t type = nondet_u8();
    __CPROVER_assume(len <= INPUT_MAX && cap <= INPUT_MAX);

    tlv8_status st = tlv8_find(buf, len, type, out, cap, &out_len);

    PG_REACH(st == TLV8_OK && out_len >= 1u, "value found");
    PG_REACH(st == TLV8_NOT_FOUND, "not found");
    PG_REACH(st == TLV8_BAD, "overrun refused");
    PG_REACH(st == TLV8_TOO_SMALL, "small output refused");

    if (st == TLV8_OK) {
        PG_PROVE(out_len <= cap, "output within capacity");
    }
    return 0;
}
