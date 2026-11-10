#include "proofgate/modbus.h"

#include <stdbool.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint32_t)p[0] << 8) | (uint32_t)p[1]);
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((uint32_t)v >> 8);
    p[1] = (uint8_t)((uint32_t)v & 0xffu);
}

static bool valid_fc(uint8_t fc)
{
    return fc == MB_FC_READ_HOLDING || fc == MB_FC_READ_INPUT;
}

mb_status mb_encode_read(uint8_t *out, size_t cap, uint16_t txid, uint8_t unit,
                         uint8_t fc, uint16_t addr, uint16_t qty)
{
    if (cap < MB_READ_REQ_LEN || !valid_fc(fc) || qty == 0u || qty > MB_MAX_REGS ||
        (uint32_t)addr + (uint32_t)qty > 65536u) {
        return MB_INVALID_ARG;
    }
    put_be16(out, txid);
    put_be16(out + 2, 0u); /* protocol id */
    put_be16(out + 4, 6u); /* unit + fc + addr + qty */
    out[6] = unit;
    out[7] = fc;
    put_be16(out + 8, addr);
    put_be16(out + 10, qty);
    return MB_OK;
}

mb_status mb_decode_read(const uint8_t *buf, size_t len, uint16_t txid, uint8_t unit,
                         uint8_t fc, uint16_t qty, uint16_t *regs, size_t regs_cap,
                         uint8_t *exception, size_t *consumed)
{
    if (!valid_fc(fc) || qty == 0u || qty > MB_MAX_REGS || regs_cap < qty) {
        return MB_INVALID_ARG;
    }
    if (len < 7u) {
        return MB_INCOMPLETE;
    }
    if (be16(buf + 2) != 0u) {
        return MB_BAD;
    }
    uint16_t field = be16(buf + 4);
    if (field < 3u || field > MB_MAX_ADU - 6u) {
        return MB_BAD;
    }
    size_t frame = 6u + (size_t)field;
    if (len < frame) {
        return MB_INCOMPLETE;
    }
    if (be16(buf) != txid || buf[6] != unit) {
        return MB_BAD;
    }

    if (buf[7] == (uint8_t)(fc | 0x80u)) {
        if (field != 3u) {
            return MB_BAD;
        }
        *exception = buf[8];
        *consumed = frame;
        return MB_EXCEPTION;
    }
    if (buf[7] != fc) {
        return MB_BAD;
    }

    size_t expect = 2u * (size_t)qty;
    if ((size_t)buf[8] != expect || (size_t)field != 3u + expect) {
        return MB_BAD;
    }
    for (size_t i = 0; i < qty; i++) {
        regs[i] = be16(buf + 9u + 2u * i);
    }
    *consumed = frame;
    return MB_OK;
}
