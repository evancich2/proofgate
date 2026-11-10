// cbmc: --unwind 26
#include "harness.h"
#include "proofgate/mqtt.h"

#define INPUT_MAX 24u

int main(void)
{
    uint8_t buf[INPUT_MAX];
    size_t len = nondet_size();
    __CPROVER_assume(len <= INPUT_MAX);

    mqtt_publish p;
    mqtt_status st = mqtt_parse_publish(buf, len, &p);

    PG_REACH(st == MQTT_OK && p.qos == 0u, "qos 0 accepted");
    PG_REACH(st == MQTT_OK && p.qos == 2u && p.payload.len >= 1u, "qos 2 with payload");
    PG_REACH(st == MQTT_INCOMPLETE, "incomplete");
    PG_REACH(st == MQTT_BAD, "bad");

    if (st == MQTT_OK) {
        PG_PROVE(p.packet_len <= len, "packet within input");
        PG_PROVE(PG_SLICE_INSIDE(p.topic, buf, p.packet_len), "topic inside packet");
        PG_PROVE(PG_SLICE_INSIDE(p.payload, buf, p.packet_len), "payload inside packet");
        PG_PROVE(p.topic.len >= 1u, "topic not empty");
        PG_PROVE(p.qos <= 2u, "qos legal");
        PG_PROVE((p.qos == 0u) == (p.packet_id == 0u), "packet id present exactly with qos");
        for (size_t i = 0; i < p.topic.len; i++) {
            PG_PROVE(p.topic.ptr[i] != 0u && p.topic.ptr[i] != '+' && p.topic.ptr[i] != '#',
                     "topic has no wildcard or NUL");
        }
    }
    return 0;
}
