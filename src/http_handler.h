#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

struct http_request;

enum handler_status {
    HANDLER_OK,          // valid, proceed to serve
    HANDLER_BAD_REQ,     // 400
    HANDLER_NOT_IMPL,    // 501
    HANDLER_BAD_VER,     // 505
};

// Checks version->method->host, needs non-NULL req filled by parse_request, after it returns PARSE_OK 
enum handler_status handle_req(const struct http_request* req);

#endif