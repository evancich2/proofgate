// cbmc: --unwind 3
//
// Complete: no input-dependent loops.
#include "harness.h"
#include "proofgate/modbus.h"

int main(void)
{
    uint8_t out[MB_READ_REQ_LEN];
    size_t cap = nondet_size();
    uint16_t txid = nondet_u16();
    uint8_t unit = nondet_u8();
    uint8_t fc = nondet_u8();
    uint16_t addr = nondet_u16();
    uint16_t qty = nondet_u16();
    __CPROVER_assume(cap <= sizeof out);

    mb_status st = mb_encode_read(out, cap, txid, unit, fc, addr, qty);

    PG_REACH(st == MB_OK, "encoded");
    PG_REACH(st == MB_INVALID_ARG, "rejected");

    if (st == MB_OK) {
        PG_PROVE(qty >= 1u && qty <= MB_MAX_REGS, "quantity legal");
        PG_PROVE((uint32_t)addr + qty <= 65536u, "range stays in the address space");
        PG_PROVE(out[0] == (uint8_t)(txid >> 8) && out[1] == (uint8_t)(txid & 0xffu), "txid");
        PG_PROVE(out[2] == 0u && out[3] == 0u, "protocol id");
        PG_PROVE(out[4] == 0u && out[5] == 6u, "length");
        PG_PROVE(out[6] == unit && out[7] == fc, "unit and function");
        PG_PROVE(out[8] == (uint8_t)(addr >> 8) && out[9] == (uint8_t)(addr & 0xffu), "address");
        PG_PROVE(out[10] == (uint8_t)(qty >> 8) && out[11] == (uint8_t)(qty & 0xffu), "quantity");
    }
    return 0;
}
