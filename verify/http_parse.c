// cbmc: --unwind 28
//
// All byte strings of every length up to INPUT_MAX, plus a larger declared
// length is not modelled here: the bound is reported in VERIFICATION.md.
#include "harness.h"
#include "proofgate/http.h"

#define INPUT_MAX 24u

int main(void)
{
    uint8_t buf[INPUT_MAX];
    size_t len = nondet_size();
    __CPROVER_assume(len <= INPUT_MAX);

    http_request req;
    http_status st = http_parse_request(buf, len, &req);

    PG_PROVE(st == HTTP_OK || st == HTTP_INCOMPLETE || st == HTTP_BAD ||
                 st == HTTP_TOO_LARGE || st == HTTP_UNSUPPORTED,
             "status is a known value");
    PG_REACH(st == HTTP_INCOMPLETE, "incomplete");
    PG_REACH(st == HTTP_BAD, "bad");
    PG_REACH(st == HTTP_OK && req.nheaders >= 1u, "accepted with a header");
    PG_REACH(st == HTTP_OK && req.query.len >= 1u, "accepted with a query");

    if (st == HTTP_OK) {
        PG_PROVE(req.head_len >= 4u && req.head_len <= len, "head_len within input");
        PG_PROVE(buf[req.head_len - 4u] == '\r' && buf[req.head_len - 3u] == '\n' &&
                     buf[req.head_len - 2u] == '\r' && buf[req.head_len - 1u] == '\n',
                 "head ends in CRLFCRLF");
        PG_PROVE(PG_SLICE_INSIDE(req.target, buf, req.head_len), "target inside head");
        PG_PROVE(PG_SLICE_INSIDE(req.path, buf, req.head_len), "path inside head");
        PG_PROVE(PG_SLICE_INSIDE(req.query, buf, req.head_len), "query inside head");
        PG_PROVE(req.target.len >= 1u && req.target.ptr[0] == '/', "origin-form target");
        PG_PROVE(req.version_minor <= 1u, "version is 1.0 or 1.1");
        PG_PROVE(req.nheaders <= HTTP_MAX_HEADERS, "header count within limit");
        PG_PROVE(req.has_content_length || req.content_length == 0u,
                 "no content length without the header");
        PG_PROVE(req.content_length <= HTTP_MAX_CONTENT_LENGTH, "content length within limit");

        for (size_t h = 0; h < req.nheaders; h++) {
            PG_PROVE(PG_SLICE_INSIDE(req.headers[h].name, buf, req.head_len),
                     "header name inside head");
            PG_PROVE(PG_SLICE_INSIDE(req.headers[h].value, buf, req.head_len),
                     "header value inside head");
            PG_PROVE(req.headers[h].name.len >= 1u, "header name not empty");
            for (size_t v = 0; v < req.headers[h].value.len; v++) {
                PG_PROVE(req.headers[h].value.ptr[v] != '\r' &&
                             req.headers[h].value.ptr[v] != '\n',
                         "no CR or LF inside a header value");
            }
        }
    }
    return 0;
}
