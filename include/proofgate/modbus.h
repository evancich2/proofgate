/* Modbus TCP client codec for function codes 3 and 4. Pure: no allocation, no I/O. */
#ifndef PROOFGATE_MODBUS_H
#define PROOFGATE_MODBUS_H

#include <stddef.h>
#include <stdint.h>

#define MB_READ_REQ_LEN 12u
#define MB_MAX_ADU 260u
#define MB_MAX_REGS 125u
#define MB_FC_READ_HOLDING 3u
#define MB_FC_READ_INPUT 4u

typedef enum {
    MB_OK = 0,
    MB_INCOMPLETE, /* need more bytes */
    MB_EXCEPTION,  /* well-formed exception reply; see *exception */
    MB_BAD,        /* malformed reply, or one that does not match the request */
    MB_INVALID_ARG /* caller asked for something Modbus cannot express */
} mb_status;

/* Writes a read-registers request into out[MB_READ_REQ_LEN]. */
mb_status mb_encode_read(uint8_t *out, size_t cap, uint16_t txid, uint8_t unit,
                         uint8_t fc, uint16_t addr, uint16_t qty);

/*
 * Decodes the reply to a read-registers request. Only MB_OK and MB_EXCEPTION
 * set *consumed. MB_OK writes exactly qty values to regs, MB_EXCEPTION
 * writes the exception code. regs_cap must be at least qty.
 */
mb_status mb_decode_read(const uint8_t *buf, size_t len, uint16_t txid, uint8_t unit,
                         uint8_t fc, uint16_t qty, uint16_t *regs, size_t regs_cap,
                         uint8_t *exception, size_t *consumed);

#endif
