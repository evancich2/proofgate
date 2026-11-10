/* Strict HTTP/1.x request head parser. Pure: no allocation, no I/O, no libc. */
#ifndef PROOFGATE_HTTP_H
#define PROOFGATE_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "proofgate/slice.h"

#define HTTP_MAX_HEAD 8192u
#define HTTP_MAX_HEADERS 32u
#define HTTP_MAX_CONTENT_LENGTH 1048576u

typedef enum {
    HTTP_OK = 0,
    HTTP_INCOMPLETE, /* no end of head yet; read more */
    HTTP_BAD,        /* malformed or ambiguous; answer 400 */
    HTTP_TOO_LARGE,  /* head or header count over the limit; answer 431 */
    HTTP_UNSUPPORTED /* Transfer-Encoding present; answer 501 */
} http_status;

typedef enum {
    HTTP_M_GET,
    HTTP_M_HEAD,
    HTTP_M_POST,
    HTTP_M_PUT,
    HTTP_M_DELETE,
    HTTP_M_OTHER
} http_method;

typedef struct {
    pg_slice name;
    pg_slice value;
} http_header;

typedef struct {
    http_method method;
    pg_slice target; /* path plus query, always starts with '/' */
    pg_slice path;
    pg_slice query; /* excludes the '?'; empty when absent */
    uint8_t version_minor; /* 0 or 1 */
    http_header headers[HTTP_MAX_HEADERS];
    size_t nheaders;
    bool has_content_length;
    size_t content_length;
    bool keep_alive;
    size_t head_len; /* bytes consumed, including the final CRLFCRLF */
} http_request;

/*
 * Parses one request head from buf[0..len). On HTTP_OK every slice in req
 * points inside buf. Deliberately stricter than the RFCs allow: bare LF,
 * obsolete line folding, duplicate Content-Length, duplicate or missing
 * Host (on 1.1) and any Transfer-Encoding are refused, which closes the
 * usual request-smuggling routes.
 */
http_status http_parse_request(const uint8_t *buf, size_t len, http_request *req);

#endif
