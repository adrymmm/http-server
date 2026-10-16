#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H
#define PATH_CAP 256
#include <stddef.h>

struct http_request;

enum method {
    METHOD_GET,
    METHOD_HEAD,
};

enum handler_status {
    HANDLER_OK,          // valid, proceed to serve
    HANDLER_BAD_REQ,     // 400
    HANDLER_NOT_IMPL,    // 501
    HANDLER_BAD_VER,     // 505
};

enum path_status {
    PATH_OK, 
    PATH_BAD_REQUEST,    // 400     
    PATH_URI_TOO_LONG,   // 414
};

// Checks version->method->host, needs non-NULL req filled by parse_request, after it returns PARSE_OK 
enum handler_status handle_req(const struct http_request* req, enum method *out);

/* Builds "www" + target into path; "/"  index.html, query dropped.
 * path is unspecified unless PATH_OK.
 * target: view, no NUL bytes. */
enum path_status target_to_path(const char* target, size_t target_len, char* path, size_t path_cap);

#endif
