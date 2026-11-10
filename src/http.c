#include "proofgate/http.h"

static bool is_tchar(uint8_t c)
{
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return true;
    }
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case '`': case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

static bool is_ows(uint8_t c)
{
    return c == ' ' || c == '\t';
}

static bool is_field_char(uint8_t c)
{
    return c == '\t' || (c >= 0x20 && c != 0x7f);
}

static uint8_t lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32u) : c;
}

/* Case-insensitive match against a lowercase NUL-terminated literal. */
static bool ci_eq(const uint8_t *p, size_t n, const char *lit)
{
    size_t i = 0;
    while (i < n) {
        if (lit[i] == '\0' || lower(p[i]) != (uint8_t)lit[i]) {
            return false;
        }
        i++;
    }
    return lit[i] == '\0';
}

/* Case-sensitive match, used for method names. */
static bool eq(const uint8_t *p, size_t n, const char *lit)
{
    size_t i = 0;
    while (i < n) {
        if (lit[i] == '\0' || p[i] != (uint8_t)lit[i]) {
            return false;
        }
        i++;
    }
    return lit[i] == '\0';
}

static http_method parse_method(const uint8_t *p, size_t n)
{
    if (eq(p, n, "GET")) {
        return HTTP_M_GET;
    }
    if (eq(p, n, "HEAD")) {
        return HTTP_M_HEAD;
    }
    if (eq(p, n, "POST")) {
        return HTTP_M_POST;
    }
    if (eq(p, n, "PUT")) {
        return HTTP_M_PUT;
    }
    if (eq(p, n, "DELETE")) {
        return HTTP_M_DELETE;
    }
    return HTTP_M_OTHER;
}

/* Digits only, no sign, and bounded so the accumulator cannot overflow. */
static bool parse_content_length(pg_slice v, size_t *out)
{
    size_t acc = 0;
    if (v.len == 0) {
        return false;
    }
    for (size_t i = 0; i < v.len; i++) {
        uint8_t c = v.ptr[i];
        if (c < '0' || c > '9') {
            return false;
        }
        acc = acc * 10u + (size_t)(c - '0');
        if (acc > HTTP_MAX_CONTENT_LENGTH) {
            return false;
        }
    }
    *out = acc;
    return true;
}

http_status http_parse_request(const uint8_t *buf, size_t len, http_request *req)
{
    size_t limit = len < HTTP_MAX_HEAD ? len : HTTP_MAX_HEAD;
    size_t term = 0;
    bool found = false;

    for (size_t k = 0; k + 4u <= limit; k++) {
        if (buf[k] == '\r' && buf[k + 1u] == '\n' && buf[k + 2u] == '\r' &&
            buf[k + 3u] == '\n') {
            term = k;
            found = true;
            break;
        }
    }
    if (!found) {
        return len >= HTTP_MAX_HEAD ? HTTP_TOO_LARGE : HTTP_INCOMPLETE;
    }

    /* end is one past the CRLF of the last header line. */
    const size_t end = term + 2u;
    size_t i = 0;
    size_t start = 0;

    req->method = HTTP_M_OTHER;
    req->nheaders = 0;
    req->has_content_length = false;
    req->content_length = 0;
    req->keep_alive = false;
    req->head_len = 0;
    req->version_minor = 0;

    /* method SP */
    while (i < end && is_tchar(buf[i])) {
        i++;
    }
    if (i == start || i >= end || buf[i] != ' ') {
        return HTTP_BAD;
    }
    req->method = parse_method(buf + start, i - start);
    i++;

    /* target SP: printable ASCII only, origin-form only */
    start = i;
    while (i < end && buf[i] > 0x20 && buf[i] < 0x7f) {
        i++;
    }
    if (i == start || i >= end || buf[i] != ' ' || buf[start] != '/') {
        return HTTP_BAD;
    }
    req->target.ptr = buf + start;
    req->target.len = i - start;
    i++;

    /* split the target at the first '?' */
    {
        size_t q = 0;
        while (q < req->target.len && req->target.ptr[q] != '?') {
            q++;
        }
        req->path.ptr = req->target.ptr;
        req->path.len = q;
        if (q < req->target.len) {
            req->query.ptr = req->target.ptr + q + 1u;
            req->query.len = req->target.len - q - 1u;
        } else {
            req->query.ptr = req->target.ptr + q;
            req->query.len = 0;
        }
    }

    /* "HTTP/1.x" CRLF */
    if (end - i < 10u || !eq(buf + i, 7u, "HTTP/1.") ||
        (buf[i + 7u] != '0' && buf[i + 7u] != '1') || buf[i + 8u] != '\r' ||
        buf[i + 9u] != '\n') {
        return HTTP_BAD;
    }
    req->version_minor = (uint8_t)(buf[i + 7u] - '0');
    req->keep_alive = req->version_minor == 1u;
    i += 10u;

    size_t host_count = 0;

    while (i < end) {
        /* field-name ":" with no whitespace before the colon */
        start = i;
        while (i < end && is_tchar(buf[i])) {
            i++;
        }
        if (i == start || i >= end || buf[i] != ':') {
            return HTTP_BAD;
        }
        pg_slice name = { buf + start, i - start };
        i++;

        while (i < end && is_ows(buf[i])) {
            i++;
        }
        size_t vstart = i;
        while (i < end && is_field_char(buf[i])) {
            i++;
        }
        if (end - i < 2u || buf[i] != '\r' || buf[i + 1u] != '\n') {
            return HTTP_BAD;
        }
        size_t vend = i;
        while (vend > vstart && is_ows(buf[vend - 1u])) {
            vend--;
        }
        pg_slice value = { buf + vstart, vend - vstart };
        i += 2u;

        if (req->nheaders == HTTP_MAX_HEADERS) {
            return HTTP_TOO_LARGE;
        }
        req->headers[req->nheaders].name = name;
        req->headers[req->nheaders].value = value;
        req->nheaders++;

        if (ci_eq(name.ptr, name.len, "transfer-encoding")) {
            return HTTP_UNSUPPORTED;
        }
        if (ci_eq(name.ptr, name.len, "content-length")) {
            if (req->has_content_length ||
                !parse_content_length(value, &req->content_length)) {
                return HTTP_BAD;
            }
            req->has_content_length = true;
        } else if (ci_eq(name.ptr, name.len, "host")) {
            host_count++;
        } else if (ci_eq(name.ptr, name.len, "connection")) {
            if (ci_eq(value.ptr, value.len, "close")) {
                req->keep_alive = false;
            } else if (ci_eq(value.ptr, value.len, "keep-alive")) {
                req->keep_alive = true;
            }
        }
    }

    if (host_count > 1u || (req->version_minor == 1u && host_count == 0u)) {
        return HTTP_BAD;
    }

    req->head_len = term + 4u;
    return HTTP_OK;
}
