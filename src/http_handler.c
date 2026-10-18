#include "http_handler.h"
#include "http_parse.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>

// checks string sizes first then return true if they match exactly -- s must be NUL-terminated
static bool s_reqmatch(const char* target, size_t tlen, const char* s) {
    if (tlen == strlen(s)) {
        return memcmp(target, s, tlen) == 0;
    }
    return false;
}

// looks for dot segments "/../" or "/.." expects first char to be '/'
static bool has_dotseg(const char* path, size_t len) {
    size_t start = 0; // index into path
    while (start < len) {
        const char *slash = memchr(path + start, '/', len - start);
        size_t end = (slash != NULL) ? (size_t) (slash - path) : len;
        if (end - start == 2 && memcmp(path + start, "..", 2) == 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

enum handler_status handle_req(const struct http_request* req, enum method *out) {
    if (!s_reqmatch(req->line.version, req->line.version_len, "HTTP/1.1")) {
        return HANDLER_BAD_VER;
    }
    
    enum method m;
    // Only accept GET and HEAD (case-sensitive)
    if (s_reqmatch(req->line.method, req->line.method_len, "GET")) {
        m = METHOD_GET; 
    }
    else if (s_reqmatch(req->line.method, req->line.method_len, "HEAD")) {
       m = METHOD_HEAD; 
    }
    else {
        return HANDLER_NOT_IMPL;
    } 
    
  
    int host_count = 0;
    for (size_t k = 0; k < req->count; k++) {
        if (req->headers[k].name_len == 4 && strncasecmp(req->headers[k].name, "host", 4) == 0)
            host_count += 1;
    }

    // host count needs to be exactly one
    if (host_count != 1) {
        return HANDLER_BAD_REQ;
    }
    *out = m;

    return HANDLER_OK;
}

enum path_status target_to_path(const char* target, size_t target_len, char* path, size_t path_cap) {
   if (target_len == 0 || target[0] != '/') return PATH_BAD_REQUEST;
    
   // path len is either the full target or ends at first ? if exists
   const char* p = memchr(target, '?', target_len);
   size_t path_len = (p != NULL) ? (size_t) (p - target) : target_len;
 
   if (has_dotseg(target, path_len)) return PATH_BAD_REQUEST;

   static const char base[] = "www";
   static const char idx[] = "index.html";
    
   // true when the last byte of the path is a slash -- will resolve to index
   bool last_slash = target[path_len - 1] == '/';
    
   // if last byte is a slash then it adds the size of index.html
   size_t out_len = (last_slash) ? (sizeof base - 1) + path_len + (sizeof idx - 1) : (sizeof base - 1) + path_len;
 
   // capped at path_cap inclusive 
   if (out_len >= path_cap) return PATH_URI_TOO_LONG;
    
   size_t pos = 0;
   memcpy(path + pos, base, sizeof base - 1);
   pos += sizeof base - 1;
   memcpy(path + pos, target, path_len);
   pos += path_len;
   if (last_slash) {
       memcpy(path + pos, idx, sizeof idx - 1);
       pos += sizeof idx - 1;
   }
   path[pos] = '\0';
   return PATH_OK;

}

const char* content_type(const char* path) {
    // fallback is octet-strem if suffix cannot be determined from table
    static const char fallback[] = "application/octet-stream";

    struct ext_type {
        const char *ext;
        const char *type;
    };

    static const struct ext_type types[] = {
        { "html", "text/html; charset=utf-8" },
        { "css",  "text/css; charset=utf-8" },
        { "js",   "text/javascript; charset=utf-8" },
        { "txt",  "text/plain; charset=utf-8" },
        { "png",  "image/png" },
        { "jpg",  "image/jpeg" },
        { "ico",  "image/x-icon" },
    };
    
    // find last slash
    const char* slash = strrchr(path, '/');
    
    // find last dot
    const char* dot = strrchr(path, '.');
    
    // points at file start
    const char* name = (slash == NULL) ? path : slash + 1;

    // No dot in path means fallback
    if (dot == NULL) return fallback;

    // Dot is before name e.g. 1.21/minecraft binary -- fallback
    if (dot < name) return fallback;

    // Dot starts name e.g. /.env -- fallback
    if (dot == name) return fallback;

    // runs when dot > slash. linear scan over array
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        if (strcasecmp(types[i].ext, dot + 1) == 0) {
            return types[i].type;
        }
    }
    return fallback;
}
