#include "http_parse.h"   
#include <ctype.h>        // isalnum
#include <stdbool.h>      // bool returns of the classifiers
#include <stdio.h>        // fprintf, stderr
#include <string.h>       // memchr, memcmp

static bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';    
}

static bool is_ctl(unsigned char c) {
    // function expects unsigned char for x86 same reason as istchar
    return c != '\t' && (c < 0x20 || c == 0x7F);
}

static bool is_ows(unsigned char c) {
    // function expects unsigned char for consistency with istchar
    return c == ' ' || c == '\t';
}

static bool is_tchar(unsigned char c) {
   // function expects unsigned char to guard against negative char values
    static const char punct[] = "!#$%&'*+-.^_`|~";
    return isalnum(c) || (memchr(punct, c, sizeof punct - 1) != NULL);
}

static int find_crlf(const char *buf, size_t pos, size_t len, size_t *crlf_idx) {
    // finds first crlf -- should be \r 
    // *crlf_idx = index of the \r

    for (size_t i = pos; i < len; i++) {
        if (buf[i] == '\r') {
            if (i + 1 < len && buf[i + 1] == '\n') {
                *crlf_idx = i;
                return 0;
            }
            return -1;
        }

        if (buf[i] == '\n') return -1;
    }
    return -1;
}

enum parse_status parse_request(const char* buf, size_t buf_size, struct http_request *out) {
    size_t pos = 0, crlf_idx;
    out->count = 0;

    // request line scan 
    int status = find_crlf(buf, pos, buf_size, &crlf_idx);
    if (status == -1) {
        fprintf(stderr, "http: parse_request: request line: does not terminate properly (pos %zu)\n", pos);
        return PARSE_BAD_REQUEST;
    } 
    status = parse_request_line(buf, crlf_idx, &out->line);
    if (status == -1) return PARSE_BAD_REQUEST;
    
    pos = crlf_idx + 2; // skip the CRLF

    while (pos < buf_size) {
        status = find_crlf(buf, pos, buf_size, &crlf_idx);
        
        if (status == -1) {
            fprintf(stderr, "http: parse_request: header line %zu: does not terminate properly (pos %zu)\n", out->count, pos);
            return PARSE_BAD_REQUEST;
        } 

        if (crlf_idx == pos) return PARSE_OK;
        if (out->count == MAX_HEADERS) {
            fprintf(stderr, "http: parse_request:header line: maximum number of header lines (%d) reached (pos %zu)\n", MAX_HEADERS, pos);
            return PARSE_HEADERS_TOO_LARGE;
        } 

        status = parse_header_line(buf + pos, crlf_idx - pos, &out->headers[out->count]);
        if (status == -1) return PARSE_BAD_REQUEST;
        out->count += 1;
        pos = crlf_idx + 2;
    }
    fprintf(stderr, "http: parse_request: no empty line before end of buffer (pos %zu)\n", pos);
    return PARSE_BAD_REQUEST;
}

int parse_request_line(const char* line, size_t len, struct http_request_line *out) {
    // request line has method target version
    
    //method
    size_t i, j;
    for (i = 0; i < len; i++) {
        if (line[i] == ' ') break;
        if (!is_tchar(line[i])) {
            fprintf(stderr, "http: parse_request_line: method uses invalid character 0x%02x at %zu\n", (unsigned char)line[i], i);
            return -1;
        } 
    }
    if (i == len) {
        fprintf(stderr, "http: parse_request_line: did not find space between method and target\n");
        return -1;
    }

    if (i == 0 ) {
        fprintf(stderr, "http: parse_request_line: method is empty\n");
        return -1; 
    }
    
    size_t target_start = i + 1;

    // target
    for (j = target_start; j < len; j++) {
        if (line[j] == ' ') break;
        if (is_ctl(line[j]) || line[j] == '\t') {
            fprintf(stderr, "http: parse_request_line: target has invalid character 0x%02x at %zu\n", (unsigned char)line[j], j);
            return -1;
        }
    }

    if (j == len) {
        fprintf(stderr, "http: parse_request_line: did not find space between target and version\n");
        return -1;
    }

    if (j == target_start) {
        fprintf(stderr, "http: parse_request_line: target is empty\n");
        return -1; 
    }
    
    // version - must be exactly 8 bytes and of the form "HTTP/X.X"
    size_t version_start = j + 1;
    if (version_start + 8 != len) {
        fprintf(stderr, "http: parse_request_line: version is not 8 bytes long\n");
        return -1;
    }

    if (!((memcmp(line + version_start, "HTTP/", 5) == 0) && is_digit(line[version_start + 5]) && is_digit(line[version_start + 7]) && line[version_start + 6] == '.')) {
        fprintf(stderr, "http: parse_request_line: malformed version\n");
        return -1;
    }

    out->method = line;
    out->method_len = i;
    out->target = line + target_start;
    out->target_len = j - target_start;
    out->version = line + version_start;
    out->version_len = 8;
    return 0;
}

int parse_header_line(const char* line, size_t len, struct http_header *out) {
    size_t i, j;
    for (i = 0; i < len; i++) {
         if (line[i] == ':') {
            break;
        }
        if (!is_tchar(line[i])) {
            fprintf(stderr, "http: parse_header_line: header name uses invalid character 0x%02x at %zu\n", (unsigned char)line[i], i);
            return -1;
        }
    }
    
    if (i == len) {
        fprintf(stderr, "http: parse_header_line: header name no colon separator\n");
        return -1; 
    }

    if (i == 0 ) {
        fprintf(stderr, "http: parse_header_line: header name has invalid placement of colon separator\n");
        return -1; 
    }
    // name valid, trim leading whitespace
    j = i + 1;
    while (j < len && is_ows(line[j])) {
        j++;
    }
    
    size_t end = j;
    size_t start = j;

    for (; j < len; j++) {
        if (is_ctl(line[j])) { 
            fprintf(stderr, "http: parse_header_line: header value has invalid ctrl sequence 0x%02x at %zu\n", (unsigned char)line[j], j);
            return -1;
        }
        if (is_ows(line[j])) continue; 
        end = j + 1;
    }

    out->name = line;
    out->name_len = i;
    out->value = line + start;
    out->value_len = end - start;
    return 0;
}

