#include "http_parse.h"
#include "http_handler.h"
#include <stdbool.h>      
#include <stdio.h>        // fprintf, stderr
#include <stdlib.h>                        
#include <string.h>
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define S(lit) (lit), (sizeof(lit) - 1)

struct handler_case {
    const char* in;     size_t in_len;
    enum handler_status status;
};

struct path_case {
    const char* in;     size_t in_len;
    const char* exp_path;
    enum path_status status;
    size_t cap;
};

static const struct path_case pcases[] = {
    // OK CASES
    { S("/"), "www/index.html", .status = PATH_OK },

    { S("/index.html"), "www/index.html", .status = PATH_OK },

    { S("/docs/a.html"), "www/docs/a.html", .status = PATH_OK },

    { S("/index.html?v=2"), "www/index.html", .status = PATH_OK },

    { S("/?x=1"), "www/index.html", .status = PATH_OK },

    { S("/a..b.html"), "www/a..b.html", .status = PATH_OK },

    { S("/..a"), "www/..a", .status = PATH_OK },
    
    { S("/docs/"), "www/docs/index.html", .status = PATH_OK },

    { S("/index.html?next=/.."), "www/index.html", .status = PATH_OK },
    
    // 400 CASES

    { S("index.html"), NULL, .status = PATH_BAD_REQUEST },

    { S("*"), NULL, .status = PATH_BAD_REQUEST },

    { S("/.."), NULL, .status = PATH_BAD_REQUEST },

    { S("/../etc/passwd"), NULL, .status = PATH_BAD_REQUEST },

    { S("/docs/../index.html"), NULL, .status = PATH_BAD_REQUEST },

    { S("/docs/.."), NULL, .status = PATH_BAD_REQUEST },

    { S("/..?x=1"), NULL, .status = PATH_BAD_REQUEST },

    // 414 Boundary Check
    { S("/"), "www/index.html", .status = PATH_OK, .cap = 15 },

    { S("/"), NULL, .status = PATH_URI_TOO_LONG, .cap = 14 },

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

static const char* path_get_enum_name(enum path_status s) {
    switch (s) {
        case PATH_BAD_REQUEST:
            return "PATH_BAD_REQ";
        case PATH_URI_TOO_LONG:
            return "PATH_URI_TOO_LONG";
        case PATH_OK:
            return "PATH_OK";
    }
    return "UNKNOWN";
}

static const char* handler_get_enum_name(enum handler_status s) {
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

static int run_path_cases(void) {
    size_t n = ARRAY_LEN(pcases);
    int fails = 0;

    for (size_t k = 0; k < n; k++) {
        const struct path_case *c = &pcases[k];
        char path_buf[PATH_CAP];
        memset(path_buf, 'X', sizeof path_buf);
        size_t cap = (c->cap != 0) ? c->cap : PATH_CAP;
        enum path_status got = target_to_path(c->in, c->in_len, path_buf, cap);
        bool status_ok = (got == c->status);
        if (!status_ok) {
            fprintf(stderr, "FAIL status mismatch on request: row %zu: got %s, want %s\n", k, path_get_enum_name(got), path_get_enum_name(c->status));
            fails++;
        }
        
        // Only check for expected st ring when got and status have same status and it is OK
        if (status_ok && got == PATH_OK && strcmp(path_buf, c->exp_path) != 0) {
            fprintf(stderr, "FAIL did not receive expected string: row %zu: got %s, want %s\n", k, path_buf, c->exp_path);
            fails++;
        }



    }
    return fails;
}

static int run_handler_cases(void) {
    size_t n = ARRAY_LEN(hcases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_request out = {0};
        const struct handler_case *c = &hcases[k];
        enum parse_status pstatus = parse_request(c->in, c->in_len, &out);
        if (pstatus != PARSE_OK) {
            fprintf(stderr, "FAIL parse on request: row %zu: parse status %d\n", k, (int)pstatus);
            fails++;
            continue;
        }
        enum handler_status got = handle_req(&out);
        bool status_ok = (got == c->status);

        if (!status_ok) {
            fprintf(stderr, "FAIL status mismatch on request: row %zu: got %s, want %s\n", k, handler_get_enum_name(got), handler_get_enum_name(c->status));
            fails++;
        }
    }

    return fails;
}

int main(void) {
    int failures = 0;

    failures += run_path_cases();
    failures += run_handler_cases();
    fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
