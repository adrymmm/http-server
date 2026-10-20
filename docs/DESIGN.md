# Design

Design notes for http-server. The overview is in [README.md](../README.md).

## Scope

| Area        | Supported                                         | Not supported / behaviour      |
|-------------|---------------------------------------------------|--------------------------------|
| Methods     | `GET`, `HEAD` (case-sensitive)                    | anything else → 501            |
| Version     | `HTTP/1.1`                                        | other `HTTP/d.d` (incl. 1.0) → 505 |
| Host        | exactly one `Host` header (name case-insensitive) | missing or duplicate → 400     |
| Connections | one request per connection, `Connection: close`   | no keep-alive, no pipelining   |
| Concurrency | single-threaded, one client at a time             | no threads, fork or event loop |
| Network     | IPv4, all interfaces, port 8080                   | no IPv6, no TLS                |
| Content     | `Content-Type` from a file-extension table        | no Range, ETag or caching headers |

- **Status codes:** 200, 400, 404, 414, 431, 500, 501, 505. Errors are fixed
  literals with `Content-Length: 0`, `Connection: close` and no body.
- **HEAD:** `GET`'s headers, real `Content-Length`, no body (RFC 9110 §9.3.2).
- **Content-Type:** `html`, `css`, `js`, `txt`, `png`, `jpg`, `ico` (any case;
  text gets `; charset=utf-8`); everything else `application/octet-stream`.

## Architecture

Each stage returns its own enum, and `respond()` in `server.c` maps those onto
wire statuses, often several onto one. The specific cause goes to the log, never
to the client.

```
 accept() ──► set 10 s SO_RCVTIMEO / SO_SNDTIMEO
    │
    ▼
 recv_delim()       framing: accumulate bytes until "\r\n\r\n"
    │               (4096-byte buffer; full → 431 + lingering close;
    │                timeout / peer closed → close silently)
    ▼
 parse_request()    syntax: pure, one pass, returns enum parse_status
    │               (malformed → 400, more than MAX_HEADERS → 431)
    ▼
 handle_req()       semantics: version → method → Host
    │               (505 / 501 / 400)
    ▼
 target_to_path()   URL → "www/..." path, checked before any filesystem access
    │               (400 / 414)
    ▼
 serve_file()       open + fstat, then send header, then stream body
    │               (404 / 500 before the header; after it, log and close)
    ▼
 close()
```

The parser is zero-copy: the request line and headers are pointer-plus-length
views into the receive buffer, valid only while it lives. Up to `MAX_HEADERS`
(32) headers sit in a fixed array, so nothing is heap-allocated.

| File                    | Contents                                                                 |
|-------------------------|--------------------------------------------------------------------------|
| `src/server.c`          | socket setup, accept loop, `recv_delim`, `respond`, `serve_file`, `send_error`, `lingering_close` |
| `src/http_parse.c/.h`   | `parse_request`, `parse_request_line`, `parse_header_line`; request structs, `MAX_HEADERS` |
| `src/http_handler.c/.h` | `handle_req`, `target_to_path`, `content_type`; `PATH_CAP`               |
| `tests/test_parse.c`    | table-driven tests for the three parse functions and the header limit    |
| `tests/test_handler.c`  | table-driven tests for `handle_req`, `target_to_path`, `content_type`    |
| `fuzz/fuzz_parse.c`     | libFuzzer entry point for `parse_request`; seeds in `fuzz/seeds/`        |
| `www/`                  | document root                                                            |
| `exercises/`            | Early socket exercises from Beej's guide (a TCP echo server, written before this server); not part of the HTTP server. |

## Design decisions

- **Strict parsing**
  - Why: request smuggling needs two parsers to disagree on where a message
    ends, so anything ambiguous is a 400: more than one SP between
    request-line tokens, a lone LF or CR (CRLF is required and nothing else
    ends the headers), whitespace before the colon, leading whitespace,
    obs-fold, and CTL or NUL in the target or header values.
  - Trade-off: some lenient, non-conforming clients get a 400.
- **Pure, one-pass parser that returns an enum**
  - Why: no I/O, so it can be unit-tested with string literals and fuzzed
    directly.
  - Trade-off: `respond()` must translate enums to statuses in one place.
- **Zero-copy views into the receive buffer**
  - Why: no allocation or copying on the request path.
  - Trade-off: views die with the buffer; that lifetime rule must be kept.
- **`target_to_path` checks the URL before `open()`**
  - Why: rejecting before touching the filesystem means a response can't
    reveal whether a file exists. The target must start with `/`; the query
    is cut at the first `?`; a trailing `/` becomes `index.html`; a segment
    that is exactly `..` → 400 (`/a..b.html` is fine); any segment starting
    with `.` → 400 (`/.env`, `/.git/config`); the built path must fit
    `PATH_CAP` (256 bytes incl. NUL) or 414.
  - Trade-off: all dotfiles are unreachable, including `/.well-known/`.
- **Opening files: `open(O_RDONLY | O_NONBLOCK | O_NOFOLLOW)`, then `fstat` on the fd**
  - Why: `fstat` on the fd I actually read avoids the `stat()`-then-`open()`
    TOCTOU race. `O_NONBLOCK` stops a writerless FIFO from blocking `open()`
    forever; `fstat` then rejects it as non-regular (the flag has no effect
    on regular files). `O_NOFOLLOW` refuses a symlink as the last path
    component (`ELOOP` → 404).
  - Trade-off: one extra syscall, and rejected files are still opened.
    `O_NOFOLLOW` covers the last component only (see README, Known
    limitations).
- **`ENOENT`, `EACCES`, `ENOTDIR`, `ELOOP` and non-regular files → 404; other errors → 500**
  - Why: `EACCES` as 403 would confirm the file exists.
  - Trade-off: debugging permissions needs the server log.
- **The header is the commit point**
  - Why: once `200 OK` is on the wire the status can't change, so every
    status-deciding check runs before the first byte is sent.
  - Trade-off: after the header, a read or send failure can only be logged
    and the connection closed; the client gets a short body.
- **Body streamed in `CHUNK_SIZE` (8192) chunks with `read` + `sendall`**
  - Why: constant memory for any file size.
  - Trade-off: every chunk is copied through a user-space buffer.
- **10 s `SO_RCVTIMEO` / `SO_SNDTIMEO` on every accepted fd; a timeout closes silently, no 408**
  - Why: a silent client can't hold the single thread forever. nginx also
    closes silently.
  - Trade-off: per call, not per request (see README, Known limitations).
- **Lingering close after a 431 for an overfull buffer**
  - Why: closing with unread data makes the kernel send RST, which can
    destroy the 431 before the client reads it. So: `shutdown(SHUT_WR)`,
    read and discard up to `DRAIN_CAP` (64 KB), then `close()`.
  - Trade-off: up to 64 KB read for an already-rejected request. Past the
    cap the RST still happens (nginx makes the same compromise).
- **`SIGPIPE` ignored**
  - Why: a send to a closed peer returns `EPIPE` instead of killing the
    process.
  - Trade-off: every send must check its return value; `sendall` does.

I verified the lingering close with `tcpdump`. With 32 KB of excess data the
connection ended in a clean FIN. With 1 MB there was a FIN, then an RST once the
drain cap was reached, and the client still received the 431.
