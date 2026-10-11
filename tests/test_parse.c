#include "http_parse.h"
#include <stdbool.h>      // bool returns of the classifiers
#include <stdio.h>        // fprintf, stderr
#include <string.h>       // memcpy, memcmp
#include <stdlib.h>                        
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define S(lit) (lit), (sizeof(lit) - 1)

struct full_req_case {
    const char* in;     size_t in_len;
    enum parse_status status;
    size_t count;
};

struct header_case {
    const char* in;     size_t in_len;
    int         rc;
    const char *name;   size_t name_len;
    const char *value;  size_t value_len;
};

struct req_line_case {
    const char* in;     size_t in_len;
    int         rc;
    const char* method; size_t method_len;
    const char* target; size_t target_len;
    const char* version; size_t version_len;
};

static bool view_eq(const char *a, size_t alen, const char *b, size_t blen) {
    return alen == blen && (memcmp(a, b, alen) == 0);
}

static const struct full_req_case full_req_cases[] = {
    // PARSE_OK
    { S("GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        "X-Inner:  a b  \r\n"
        "X-Spaces:   hi   \r\n\r\n"), 
        .status = PARSE_OK, .count = 3 },

    { S("M-SEARCH * HTTP/1.1\r\n"
        "Host: x\r\n"
        "X-Inner:  a b  \r\n\r\n"),
        .status = PARSE_OK, .count = 2 },

    { S("M-SEARCH * HTTP/1.1\r\n\r\n"),
        .status = PARSE_OK, .count = 0 }, // EMPTY HEADER CHECK -- count 0 is ok

    // PARSE_BAD_REQUEST
    { S("G@T / HTTP/1.1\r\n" // BAD
        "Host: x \r\n"
        "X-Inner:  a b  \r\n\r\n"),
        .status = PARSE_BAD_REQUEST, .count = 0 },

    { S("GET / HTTP/1.1\r\n"
        "Host: x \r\n"
        "X-Inner:  a b  \r\n"
        "X-Bad: a\x01" "b" "\r\n\r\n"), // BAD
        .status = PARSE_BAD_REQUEST, .count = 2 },

    { S("GET / HTTP/1.1\n" // BAD -- LONE \n
        "Host: x \r\n"
        "X-Inner:  a b  \r\n\r\n"),
        .status = PARSE_BAD_REQUEST, .count = 0 },

    { S("GET / HTTP/1.1\r" // BAD -- lone \r
        "Host: x \r\n"
        "X-Inner:  a b  \r\n\r\n"),
        .status = PARSE_BAD_REQUEST, .count = 0 },

    { S("GET / HTTP/1.1\r\n"
        "Host: x\r\n"),
        .status = PARSE_BAD_REQUEST, .count = 1 },

    { S("GET / HTTP/1.1\r\n"
        "Host: x\r\n"
        " folded\r\n"
        "\r\n"),
        .status = PARSE_BAD_REQUEST, .count = 1 }, 
};

static const struct header_case header_cases[] = {
    { S("Host: x"),           0, S("Host"),     S("x")  },
    { S("X-Empty:"),          0, S("X-Empty"),  S("")   },
    { S("X-Nul: a\0b"),      .rc = -1 },   
    { S("X-Spaces:   hi   "),    0, S("X-Spaces"), S("hi")           },
    { S("X-Tab:\thi\t"),         0, S("X-Tab"),    S("hi")           },
    { S("X-Inner:  a b  "),      0, S("X-Inner"),  S("a b")          },
    { S("Host: 1.2.3.4:8080"),   0, S("Host"),     S("1.2.3.4:8080") },
    { S("Accept: */*"),          0, S("Accept"),   S("*/*")          },
    { S("X-Test: caf\xc3\xa9"),  0, S("X-Test"),   S("caf\xc3\xa9")  },
    { S("Host : x"),             .rc = -1 },
    { S(":x"),                   .rc = -1 },
    { S("NoColon"),              .rc = -1 },
    { S(" Host: x"),             .rc = -1 },
    { S("X-Bad: a\x01" "b"),     .rc = -1 },
    { S("X-Del: a\x7f"),         .rc = -1 },
    { S(""),                     .rc = -1 },
};

static const struct req_line_case request_cases[] = {
    /* accept */
    { S("GET / HTTP/1.1"),            0, S("GET"),      S("/"),          S("HTTP/1.1") },
    { S("DELETE /x HTTP/2.0"),        0, S("DELETE"),   S("/x"),         S("HTTP/2.0") },
    { S("M-SEARCH * HTTP/1.1"),       0, S("M-SEARCH"), S("*"),          S("HTTP/1.1") },
    { S("get / HTTP/1.1"),            0, S("get"),      S("/"),          S("HTTP/1.1") },
    { S("GET /a%20b?q=1 HTTP/1.0"),   0, S("GET"),      S("/a%20b?q=1"), S("HTTP/1.0") },
    { S("GET / HTTP/0.9"),            0, S("GET"),      S("/"),          S("HTTP/0.9") },
    { S("GET / HTTP/9.9"),            0, S("GET"),      S("/"),          S("HTTP/9.9") },
    /* reject */
    { S("G@T / HTTP/1.1"),            .rc = -1 },
    { S(" GET / HTTP/1.1"),           .rc = -1 },
    { S("GET  / HTTP/1.1"),           .rc = -1 },
    { S("GET  HTTP/1.1"),             .rc = -1 },
    { S("GET /"),                     .rc = -1 },
    { S("GET / HTTP/1x1"),            .rc = -1 },
    { S("GET / http/1.1"),            .rc = -1 },
    { S("GET / HTTP//.1"),            .rc = -1 },
    { S("GET / HTTP/:.1"),            .rc = -1 },
    { S("GET / HTTP/1.10"),           .rc = -1 },
    { S("GET / HTTP/1.1 extra"),      .rc = -1 },
    { S("GET / HTTP/1.1\r"),          .rc = -1 },
    { S("GET /a\x01" "b HTTP/1.1"),   .rc = -1 },
    { S("GET /a\tb HTTP/1.1"),        .rc = -1 },
    { S("GET /a\0b HTTP/1.1"),        .rc = -1 },

};

// small helper that maps parse_status enum names as strings for display
static const char* get_enum_name(enum parse_status s) {
    switch (s) {
        case PARSE_OK:
            return "PARSE_OK";
        case PARSE_BAD_REQUEST:
            return "PARSE_BAD_REQUEST";
        case PARSE_HEADERS_TOO_LARGE:
            return "PARSE_HEADERS_TOO_LARGE";
    }
    return "UNKNOWN";
}

int run_full_request(void) {
    size_t n = ARRAY_LEN(full_req_cases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_request out = {0};
        const struct full_req_case *c = &full_req_cases[k];
        enum parse_status got = parse_request(c->in, c->in_len, &out);
        bool status_ok = (got == c->status);
        bool count_ok = (out.count == c->count);

        if (!status_ok) {
            fprintf(stderr, "FAIL status mismatch on request: got %s, want %s | at row %zu\n", get_enum_name(got), get_enum_name(c->status), k);
        }

        if (!count_ok) {
            fprintf(stderr, "FAIL got wrong count: got %zu, want %zu | at row %zu\n", out.count, c->count, k);
        }

        if (!status_ok || !count_ok) fails++;
    }
    return fails;
}

static int fill_buf(char* buf, size_t buf_size, size_t *pos, const char* src, size_t src_size) {
    if (*pos + src_size > buf_size) return -1;

    memcpy(buf + *pos, src, src_size);
    *pos += src_size;
    return 0;
}

static int build_buf(char* buf, size_t cap, size_t n, size_t *len) {
    size_t pos = 0;
    static const char req[] = "GET / HTTP/1.1\r\n";  
    static const char hdr[] = "X: y\r\n";            

    int status = fill_buf(buf, cap, &pos, req, sizeof(req)- 1);
    if (status == -1) {
        fprintf(stderr, "build_buf: request line exceeded buffer size\n"); 
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        status = fill_buf(buf, cap, &pos, hdr, sizeof(hdr) - 1);
        if (status == -1) {
            fprintf(stderr, "build_buf: header line exceeded buffer size\n");
            return -1;
        }
    }
    status = fill_buf(buf, cap, &pos, "\r\n", 2);
    if (status == -1) {
        fprintf(stderr, "build_buf: last blank line overflows buffer\n");
        return -1;
    }
    *len = pos;
    return 0;
}

int run_header_limit(void) {
    int fails = 0;
    char buf[1024]; // exact bytes needed are 216 (16 + 33*6 + 2) to test 32 and 33 header case

    // 32 HEADER BLOCK
    {

        size_t len = 0;
        if (build_buf(buf, sizeof(buf), MAX_HEADERS, &len) == -1) {
            fprintf(stderr, "FAIL run_header_limit: 32 case: build failed\n");
            fails++;
        }
        else {
            struct http_request out = {0};
            enum parse_status got = parse_request(buf, len, &out);

            if (got != PARSE_OK) {
                fprintf(stderr, "FAIL run_header_limit: edge case 32 headers got %s, expected %s\n", get_enum_name(got), get_enum_name(PARSE_OK));
                fails += 1;
            }
            // COUNT CHECK
            if (out.count != MAX_HEADERS) {
                fprintf(stderr, "FAIL: run_header_limit: edge case 32 count check got %zu, expected %d\n", out.count, MAX_HEADERS);
                fails += 1;
            }
        }
        
    }
    // 33 HEADER BLOCK
    {

        size_t len = 0;
        if (build_buf(buf, sizeof(buf), MAX_HEADERS + 1, &len) == -1) {
            fprintf(stderr, "FAIL run_header_limit: 33 case: build failed\n");
            fails++;
        }
        else {
            struct http_request out = {0};
            enum parse_status got = parse_request(buf, len, &out);

            if (got != PARSE_HEADERS_TOO_LARGE) {
                fprintf(stderr, "FAIL run_header_limit: 33 headers got %s, expected %s\n", get_enum_name(got), get_enum_name(PARSE_HEADERS_TOO_LARGE));
                fails +=1;
            }
            // COUNT CHECK
            if (out.count != MAX_HEADERS) {
                fprintf(stderr, "FAIL: run_header_limit: 33 count check got %zu, expected %d\n", out.count, MAX_HEADERS);
                fails += 1;
            }

        }
    
    }

    return fails;
}

int run_header_cases(void) {
    size_t n = ARRAY_LEN(header_cases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_header out = {0};
        const struct header_case *c = &header_cases[k]; // pointer to curr row
        int got = parse_header_line(c->in, c->in_len, &out); //what the parser returns
        bool ok = (got == c->rc);
        if (ok && got == 0) {                                
            // if same return message we check if name and values are identical
            ok = view_eq(out.name,  out.name_len,  c->name,  c->name_len) &&
                 view_eq(out.value, out.value_len, c->value, c->value_len);
        }

        if (!ok) {
            if (got != c->rc)
                fprintf(stderr, "FAIL header row %zu: got %d, want %d\n", k, got, c->rc);
            else
                fprintf(stderr, "FAIL header row %zu: views differ\n", k);
            if (got == 0) {
                fprintf(stderr, "  name  [%.*s]\n", (int)out.name_len,  out.name);
                fprintf(stderr, "  value [%.*s]\n", (int)out.value_len, out.value);
            }
        }
        
        fails += !ok;
    }
    return fails;
}


int run_req_line_cases(void) {
    size_t n = ARRAY_LEN(request_cases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_request_line out = {0};
        const struct req_line_case *c = &request_cases[k]; // pointer to curr row
        int got = parse_request_line(c->in, c->in_len, &out); //what the parser returns
        bool ok = (got == c->rc);
        if (ok && got == 0) {                                
            // if same return message we check if name and values are identical
            ok = view_eq(out.method,  out.method_len,  c->method,  c->method_len) &&
                 view_eq(out.target, out.target_len, c->target, c->target_len) && view_eq(out.version, out.version_len, c->version, c->version_len);
        }

        if (!ok) {
            fprintf(stderr, "FAIL request row %zu: got %d, want %d\n", k, got, c->rc);
            if (got == 0) {
                fprintf(stderr, "  method  [%.*s]\n", (int)out.method_len,  out.method);
                fprintf(stderr, "  target [%.*s]\n", (int)out.target_len, out.target);
                fprintf(stderr, "  version [%.*s]\n", (int)out.version_len, out.version);
            }
        }
        
        fails += !ok;
    }
    return fails;
}

int main(void) {
    int failures = 0;
    failures += run_header_cases();
    failures += run_req_line_cases();
    failures += run_full_request();
    failures += run_header_limit();

    fprintf(stderr, "%d failure(s)\n", failures);
    return failures == 0 ? 0 : EXIT_FAILURE;
}

