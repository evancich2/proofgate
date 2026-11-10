// cbmc: --unwind 262
//
// A full 255-byte fragment followed by a continuation.
#include "harness.h"
#include "proofgate/tlv8.h"

#define INPUT_MAX 262u
#define OUT_MAX 258u

int main(void)
{
    uint8_t buf[INPUT_MAX];
    uint8_t out[OUT_MAX];
    size_t out_len = 0;
    uint8_t type = nondet_u8();
    size_t cap = nondet_size();
    __CPROVER_assume(cap <= OUT_MAX);

    /* First fragment is always full, so continuation logic must run. */
    __CPROVER_assume(buf[0] == type && buf[1] == 255u);

    tlv8_status st = tlv8_find(buf, INPUT_MAX, type, out, cap, &out_len);

    PG_REACH(st == TLV8_OK && out_len > 255u, "reassembled across fragments");
    PG_REACH(st == TLV8_TOO_SMALL, "small output refused");

    if (st == TLV8_OK) {
        PG_PROVE(out_len >= 255u && out_len <= cap, "output length");
        for (size_t i = 0; i < 255u; i++) {
            PG_PROVE(out[i] == buf[2u + i], "first fragment copied exactly");
        }
    }
    return 0;
}
