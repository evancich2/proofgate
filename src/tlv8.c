#include "proofgate/tlv8.h"

#include <stdbool.h>

tlv8_status tlv8_next(const uint8_t *buf, size_t len, size_t *offset, tlv8_item *item)
{
    size_t off = *offset;
    if (off >= len) {
        return TLV8_END;
    }
    if (len - off < 2u) {
        return TLV8_BAD;
    }
    size_t vlen = buf[off + 1u];
    if (len - off - 2u < vlen) {
        return TLV8_BAD;
    }
    item->type = buf[off];
    item->value.ptr = buf + off + 2u;
    item->value.len = vlen;
    *offset = off + 2u + vlen;
    return TLV8_OK;
}

tlv8_status tlv8_find(const uint8_t *buf, size_t len, uint8_t type, uint8_t *out,
                      size_t cap, size_t *out_len)
{
    size_t off = 0;
    size_t written = 0;
    bool in_value = false;
    bool more = false;
    tlv8_item item;

    for (;;) {
        tlv8_status st = tlv8_next(buf, len, &off, &item);
        if (st == TLV8_END) {
            break;
        }
        if (st != TLV8_OK) {
            return st;
        }
        if (in_value && (!more || item.type != type)) {
            break;
        }
        if (item.type != type) {
            continue;
        }
        if (cap - written < item.value.len) {
            return TLV8_TOO_SMALL;
        }
        for (size_t i = 0; i < item.value.len; i++) {
            out[written + i] = item.value.ptr[i];
        }
        written += item.value.len;
        in_value = true;
        more = item.value.len == 255u;
    }

    if (!in_value) {
        return TLV8_NOT_FOUND;
    }
    *out_len = written;
    return TLV8_OK;
}
