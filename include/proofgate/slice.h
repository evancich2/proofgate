/* A borrowed, length-delimited view of bytes. Never owns and never NUL-terminated. */
#ifndef PROOFGATE_SLICE_H
#define PROOFGATE_SLICE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *ptr;
    size_t len;
} pg_slice;

#endif
