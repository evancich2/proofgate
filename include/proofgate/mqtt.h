/* MQTT 3.1.1 PUBLISH packet decoder. Pure: no allocation, no I/O. */
#ifndef PROOFGATE_MQTT_H
#define PROOFGATE_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proofgate/slice.h"

#define MQTT_MAX_REMAINING 268435455u

typedef enum {
    MQTT_OK = 0,
    MQTT_INCOMPLETE,
    MQTT_BAD
} mqtt_status;

typedef struct {
    bool dup;
    bool retain;
    uint8_t qos;
    uint16_t packet_id; /* 0 when qos is 0 */
    pg_slice topic;
    pg_slice payload;
    size_t packet_len;
} mqtt_publish;

/* Decodes the Remaining Length field that starts at buf[0]. */
mqtt_status mqtt_remaining_length(const uint8_t *buf, size_t len, uint32_t *value,
                                  size_t *nbytes);

/* Decodes one PUBLISH packet. On MQTT_OK all slices point inside buf. */
mqtt_status mqtt_parse_publish(const uint8_t *buf, size_t len, mqtt_publish *out);

#endif
