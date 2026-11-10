// cbmc: --unwind 9
//
// The scan limit, shown with a reduced HTTP_MAX_HEAD: input at or past the
// limit with no terminator inside it is refused, however long it is.
#define HTTP_LIMIT_FOR_PROOF 8u
#include "harness.h"
#include "proofgate/http.h"

#undef HTTP_MAX_HEAD
#define HTTP_MAX_HEAD HTTP_LIMIT_FOR_PROOF
#include "../src/http.c"

int main(void)
{
    uint8_t buf[12];
    size_t len = nondet_size();
    __CPROVER_assume(len <= sizeof buf);

    http_request req;
    http_status st = http_parse_request(buf, len, &req);

    PG_REACH(st == HTTP_TOO_LARGE, "too large");
    if (len >= HTTP_MAX_HEAD) {
        PG_PROVE(st != HTTP_INCOMPLETE, "never waits forever at the limit");
    }
    return 0;
}
