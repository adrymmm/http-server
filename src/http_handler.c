#include "http_handler.h"
#include "http_parse.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

static bool s_reqmatch(const char* target, size_t tlen, const char* s) {
    // checks if size of string to match is equal to target and then memmcp
    if (tlen == strlen(s)) {
        return memcmp(target, s, tlen) == 0;
    }
    return false;
}

enum handler_status handle_req(const struct http_request* req) {
    if (!s_reqmatch(req->line.version, req->line.version_len, "HTTP/1.1")) {
        return HANDLER_BAD_VER;
    }

    // Only accept GET and HEAD (case-sensitive)
    if (!(s_reqmatch(req->line.method, req->line.method_len, "GET") ||
        s_reqmatch(req->line.method, req->line.method_len, "HEAD"))) {
        return HANDLER_NOT_IMPL;
    }
    int host_count = 0;
    for (size_t k = 0; k < req->count; k++) {
        if (req->headers[k].name_len == 4 && strncasecmp(req->headers[k].name, "host", 4) == 0)
            host_count += 1;
    }

    // if host found exactly once case-insensitive return OK
    return host_count == 1 ? HANDLER_OK : HANDLER_BAD_REQ; 
}