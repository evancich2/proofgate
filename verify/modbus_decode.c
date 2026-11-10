// cbmc: --unwind 127
//
// Complete for this function: every reply the decoder can read is at most
// MB_MAX_ADU bytes, so this covers the whole input space, for every request
// parameter.
#include "harness.h"
#include "proofgate/modbus.h"

int main(void)
{
    uint8_t buf[MB_MAX_ADU + 8u];
    uint16_t regs[MB_MAX_REGS];
    uint8_t exception = 0;
    size_t consumed = 0;
    size_t len = nondet_size();
    uint16_t txid = nondet_u16();
    uint8_t unit = nondet_u8();
    uint8_t fc = nondet_u8();
    uint16_t qty = nondet_u16();
    __CPROVER_assume(len <= sizeof buf);

    mb_status st = mb_decode_read(buf, len, txid, unit, fc, qty, regs, MB_MAX_REGS,
                                  &exception, &consumed);

    PG_REACH(st == MB_OK && qty == MB_MAX_REGS, "largest read decoded");
    PG_REACH(st == MB_EXCEPTION, "exception decoded");
    PG_REACH(st == MB_INCOMPLETE, "incomplete");
    PG_REACH(st == MB_BAD, "bad");

    if (st == MB_OK || st == MB_EXCEPTION) {
        PG_PROVE(consumed >= 9u && consumed <= len && consumed <= MB_MAX_ADU,
                 "consumed within input and frame limit");
        PG_PROVE(buf[6] == unit, "reply is for the requested unit");
        PG_PROVE((uint16_t)(((uint32_t)buf[0] << 8) | buf[1]) == txid,
                 "reply matches the transaction");
    }
    if (st == MB_OK) {
        PG_PROVE(qty >= 1u && qty <= MB_MAX_REGS, "quantity in range");
        PG_PROVE(buf[7] == fc, "reply has the requested function");
        PG_PROVE(consumed == 9u + 2u * (size_t)qty, "frame length matches quantity");
        for (size_t i = 0; i < qty; i++) {
            PG_PROVE(regs[i] == (uint16_t)(((uint32_t)buf[9u + 2u * i] << 8) |
                                           buf[10u + 2u * i]),
                     "registers are the big-endian payload");
        }
    }
    return 0;
}
