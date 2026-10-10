#include "http_parse.h"
#include <stdbool.h>      // bool returns of the classifiers
#include <stdio.h>        // fprintf, stderr
#include <string.h>       // memchr, memcmp
#include <stdlib.h>                        
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define S(lit) (lit), (sizeof(lit) - 1)

struct header_case {
    const char* in;     size_t in_len;
    int         rc;
    const char *name;   size_t name_len;
    const char *value;  size_t value_len;
};

struct req_case {
    const char* in;     size_t in_len;
    int         rc;
    const char* method; size_t method_len;
    const char* target; size_t target_len;
    const char* version; size_t version_len;
};

static bool view_eq(const char *a, size_t alen, const char *b, size_t blen) {
    return alen == blen && (memcmp(a, b, alen) == 0);
}


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

static const struct req_case request_cases[] = {
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
                printf("FAIL header row %zu: got %d, want %d\n", k, got, c->rc);
            else
                printf("FAIL header row %zu: views differ\n", k);
            if (got == 0) {
                printf("  name  [%.*s]\n", (int)out.name_len,  out.name);
                printf("  value [%.*s]\n", (int)out.value_len, out.value);
            }
        }
        
        fails += !ok;
    }
    return fails;
}


int run_req_cases(void) {
    size_t n = ARRAY_LEN(request_cases);
    int fails = 0;
    for (size_t k = 0; k < n; k++) {
        struct http_request_line out = {0};
        const struct req_case *c = &request_cases[k]; // pointer to curr row
        int got = parse_request_line(c->in, c->in_len, &out); //what the parser returns
        bool ok = (got == c->rc);
        if (ok && got == 0) {                                
            // if same return message we check if name and values are identical
            ok = view_eq(out.method,  out.method_len,  c->method,  c->method_len) &&
                 view_eq(out.target, out.target_len, c->target, c->target_len) && view_eq(out.version, out.version_len, c->version, c->version_len);
        }

        if (!ok) {
            printf("FAIL request row %zu: got %d, want %d\n", k, got, c->rc);
            if (got == 0) {
                printf("  method  [%.*s]\n", (int)out.method_len,  out.method);
                printf("  target [%.*s]\n", (int)out.target_len, out.target);
                printf("  version [%.*s]\n", (int)out.version_len, out.version);
            }
        }
        
        fails += !ok;
    }
    return fails;
}

int main(void) {
    int failures = 0;
    failures += run_header_cases();
    failures += run_req_cases();

    printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : EXIT_FAILURE;
}

