/*
 * TLV8 codec, the framing used by HomeKit Accessory Protocol pairing messages.
 * Pure: no allocation, no I/O.
 */
#ifndef PROOFGATE_TLV8_H
#define PROOFGATE_TLV8_H

#include <stddef.h>
#include <stdint.h>

#include "proofgate/slice.h"

typedef enum {
    TLV8_OK = 0,
    TLV8_END,      /* no more items */
    TLV8_NOT_FOUND,
    TLV8_BAD,      /* item overruns the buffer */
    TLV8_TOO_SMALL /* caller's output buffer cannot hold the value */
} tlv8_status;

typedef struct {
    uint8_t type;
    pg_slice value;
} tlv8_item;

/* Reads the item at *offset and advances *offset past it. */
tlv8_status tlv8_next(const uint8_t *buf, size_t len, size_t *offset, tlv8_item *item);

/*
 * Finds the first item of the given type and copies its value to out.
 * A 255-byte fragment continues into the next item when it has the same type.
 */
tlv8_status tlv8_find(const uint8_t *buf, size_t len, uint8_t type, uint8_t *out,
                      size_t cap, size_t *out_len);

#endif
