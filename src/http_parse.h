#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H
#include <stddef.h>
#define MAX_HEADERS 32

enum parse_status {
    PARSE_OK,
    PARSE_BAD_REQUEST,
    PARSE_HEADERS_TOO_LARGE,
};

struct http_header {
    const char* name;
    size_t name_len;
    const char* value;
    size_t value_len;
};


struct http_request_line {
    const char* method;
    size_t method_len;
    const char* target;
    size_t target_len;
    const char* version;
    size_t version_len;
};

struct http_request {
    struct http_request_line line;
    struct http_header headers[MAX_HEADERS]; 
    size_t count;
};

// enum parse_status parse_request(const char* line, size_t len, http_request_line *out);

// Returns 0 on success, -1 on fail
int parse_request_line(const char* line, size_t len, struct http_request_line *out);

// Returns 0 on success, -1 on fail
int parse_header_line(const char* line, size_t len, struct http_header *out);

#endif
