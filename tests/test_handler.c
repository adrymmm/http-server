#include "http_parse.h"
#include "http_handler.h"
#include <stdbool.h>      
#include <stdio.h>        // fprintf, stderr
#include <string.h>       // memcpy, memcmp
#include <stdlib.h>                        
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define S(lit) (lit), (sizeof(lit) - 1)

struct handler_case {
    const char* in;     size_t in_len;
    enum handler_status status;
};

static const struct handler_case hcases[] = {
    { S("GET / HTTP/1.1\r\n"
        "Host: a\r\n\r\n"),
        .status = HANDLER_OK },

    { S("get / HTTP/1.1\r\n"
        "Host: a\r\n\r\n"),
        .status = HANDLER_NOT_IMPL },
    
    { S("get / HTTP/1.1\r\n\r\n"),
        .status = HANDLER_NOT_IMPL },    
    
    { S("GET / HTTP/2.0\r\n"
        "Host: a\r\n\r\n"),
        .status = HANDLER_BAD_VER },
        
    { S("GET / HTTP/1.1\r\n\r\n"),
        .status = HANDLER_BAD_REQ },
    
    { S("get / HTTP/2.0\r\n\r\n"),
        .status = HANDLER_BAD_VER },

    { S("GET / HTTP/1.1\r\n"
        "host: a\r\n\r\n"),         //lowercase
        .status = HANDLER_OK },

    { S("GET / HTTP/1.1\r\n"
        "hOsT: a\r\n\r\n"),         // Case-insensitive test
        .status = HANDLER_OK },

    { S("GET / HTTP/1.1\r\n"
        "host: a\r\n"
        "Host: b\r\n\r\n"),
        .status = HANDLER_BAD_REQ },

    { S("HEAD / HTTP/1.1\r\n"
        "Host: a\r\n\r\n"),
        .status = HANDLER_OK },
    
    { S("head / HTTP/1.1\r\n"
        "Host: a\r\n\r\n"),
        .status = HANDLER_NOT_IMPL },
    
    { S("GET / HTTP/1.0\r\n\r\n"),
        .status = HANDLER_BAD_VER },
};

static const char* get_enum_name(enum handler_status s) {
    switch (s) {
        case HANDLER_BAD_REQ:
            return "HANDLER_BAD_REQ";
        case HANDLER_BAD_VER:
            return "HANDLER_BAD_VER";
        case HANDLER_NOT_IMPL:
            return "HANDLER_NOT_IMPL";
        case HANDLER_OK:
            return "HANDLER_OK";
    }
    return "UNKNOWN";
}


static int run_handler_cases(void) {
    size_t n = ARRAY_LEN(hcases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_request out = {0};
        const struct handler_case *c = &hcases[k];
        enum parse_status pstatus = parse_request(c->in, c->in_len, &out);
        if (pstatus != PARSE_OK) {
            fprintf(stderr, "TODO:FILLTHISIN");
            fails++;
            continue;
        }
        enum handler_status got = handle_req(&out);
        bool status_ok = (got == c->status);

        if (!status_ok) {
            fprintf(stderr, "FAIL status mismatch on request: row %zu: got %s, want %s\n", k, get_enum_name(got), get_enum_name(c->status));
            fails++;
        }
    }

    return fails;
}

int main(void) {
    int failures = 0;

    failures += run_handler_cases();
    fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}