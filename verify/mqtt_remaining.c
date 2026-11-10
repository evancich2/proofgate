// cbmc: --unwind 6
//
// Complete: the field is at most four bytes.
#include "harness.h"
#include "proofgate/mqtt.h"

int main(void)
{
    uint8_t buf[6];
    size_t len = nondet_size();
    uint32_t value = 0;
    size_t nbytes = 0;
    __CPROVER_assume(len <= sizeof buf);

    mqtt_status st = mqtt_remaining_length(buf, len, &value, &nbytes);

    PG_REACH(st == MQTT_OK && nbytes == 4u, "four-byte length");
    PG_REACH(st == MQTT_BAD, "five continuation bytes refused");
    PG_REACH(st == MQTT_INCOMPLETE, "incomplete");

    if (st == MQTT_OK) {
        PG_PROVE(nbytes >= 1u && nbytes <= 4u && nbytes <= len, "field size");
        PG_PROVE(value <= MQTT_MAX_REMAINING, "value within the protocol maximum");
    }
    return 0;
}
