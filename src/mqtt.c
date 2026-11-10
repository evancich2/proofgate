#include "proofgate/mqtt.h"

mqtt_status mqtt_remaining_length(const uint8_t *buf, size_t len, uint32_t *value,
                                  size_t *nbytes)
{
    uint32_t acc = 0;
    for (size_t i = 0; i < 4u; i++) {
        if (i >= len) {
            return MQTT_INCOMPLETE;
        }
        acc |= ((uint32_t)buf[i] & 0x7fu) << (7u * (uint32_t)i);
        if ((buf[i] & 0x80u) == 0u) {
            *value = acc;
            *nbytes = i + 1u;
            return MQTT_OK;
        }
    }
    return MQTT_BAD;
}

mqtt_status mqtt_parse_publish(const uint8_t *buf, size_t len, mqtt_publish *out)
{
    if (len < 1u) {
        return MQTT_INCOMPLETE;
    }
    uint8_t first = buf[0];
    if ((first >> 4) != 3u) {
        return MQTT_BAD;
    }
    uint8_t qos = (uint8_t)((first >> 1) & 3u);
    bool dup = (first & 8u) != 0u;
    if (qos == 3u || (qos == 0u && dup)) {
        return MQTT_BAD;
    }

    uint32_t remaining = 0;
    size_t nbytes = 0;
    mqtt_status st = mqtt_remaining_length(buf + 1, len - 1u, &remaining, &nbytes);
    if (st != MQTT_OK) {
        return st;
    }

    size_t head = 1u + nbytes;
    if (len - head < (size_t)remaining) {
        return MQTT_INCOMPLETE;
    }
    size_t end = head + (size_t)remaining;

    size_t i = head;
    if (end - i < 2u) {
        return MQTT_BAD;
    }
    size_t topic_len = ((size_t)buf[i] << 8) | (size_t)buf[i + 1u];
    i += 2u;
    if (topic_len == 0u || end - i < topic_len) {
        return MQTT_BAD;
    }
    const uint8_t *topic = buf + i;
    for (size_t k = 0; k < topic_len; k++) {
        /* A PUBLISH topic may not hold wildcards or U+0000. */
        if (topic[k] == 0u || topic[k] == '+' || topic[k] == '#') {
            return MQTT_BAD;
        }
    }
    i += topic_len;

    uint16_t packet_id = 0;
    if (qos > 0u) {
        if (end - i < 2u) {
            return MQTT_BAD;
        }
        packet_id = (uint16_t)(((uint32_t)buf[i] << 8) | (uint32_t)buf[i + 1u]);
        if (packet_id == 0u) {
            return MQTT_BAD;
        }
        i += 2u;
    }

    out->dup = dup;
    out->retain = (first & 1u) != 0u;
    out->qos = qos;
    out->packet_id = packet_id;
    out->topic.ptr = topic;
    out->topic.len = topic_len;
    out->payload.ptr = buf + i;
    out->payload.len = end - i;
    out->packet_len = end;
    return MQTT_OK;
}
