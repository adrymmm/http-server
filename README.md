# http-server

A static file server for HTTP/1.1, written in C17 from scratch on POSIX sockets.
Learning project covering TCP framing, request parsing, safe
URL-to-file mapping, streaming responses and connection teardown, tested with
unit tests, sanitizers and libFuzzer.

## Quick start

```sh
make                 # builds build/server (with ASan + UBSan, -O0 -g); needs Linux, cc, make
./build/server       # serves ./www on port 8080 from the repo root; logs to stderr
curl -i http://127.0.0.1:8080/                              # in another terminal
curl -I http://127.0.0.1:8080/index.html                    # HEAD
curl -i --path-as-is http://127.0.0.1:8080/../etc/passwd    # 400
make test            # unit tests, built with -fsanitize=address,undefined
make fuzz            # 10-minute libFuzzer run on parse_request (needs clang)
```

## Example

```sh
$ printf 'GET / HTTP/1.1\r\nHost x\r\n\r\n' | nc 127.0.0.1 8080   # missing colon
HTTP/1.1 400 Bad Request
Content-Length: 0
Connection: close
```
Server log: `http: parse_header_line: header name uses invalid character 0x20 at 4`

<img src="https://http.cat/images/500.jpg" alt="HTTP 500 cat" width="400">

## Design choices

`accept -> recv_delim -> parse_request -> handle_req -> target_to_path -> serve_file`

- **Strict, pure, zero-copy parser:** one pass, no heap allocation, rejects ambiguous input.
- **Safe URL-to-file mapping:** `..` and dotfiles rejected before touching the filesystem; `O_NOFOLLOW` / `O_NONBLOCK`; `fstat` on the opened fd to avoid TOCTOU races.
- **Status decided before responding:** every check that decides the status runs before the header is sent.
- **Graceful teardown:** lingering close so an RST can't destroy the 431 response (verified with `tcpdump`).
- **Constant-memory streaming** in 8 KB chunks, for any file size.
- **Fuzzed:** ~118M inputs under libFuzzer + ASan/UBSan, no crashes.

## Scope

`GET` and `HEAD` over `HTTP/1.1`; anything else gets 501 or 505.
One request per connection (`Connection: close`), one client at a time.
Single-threaded, IPv4 only, port 8080, serving `www/`.

Details: [docs/DESIGN.md](docs/DESIGN.md).

## Testing

- **Unit tests (`make test`):** table-driven, built with ASan + UBSan, each limit tested at the limit and one past it.
- **Mutation checks:** code was broken on purpose to confirm the expected rows fail.
- **Fuzzing (`make fuzz`)**: libFuzzer on `parse_request`, each input in an exact-size heap buffer.

## Known limitations

- **Single-threaded:** one slow client delays everyone (fix: fork, threads or an epoll loop).
- **Timeouts are per call, not per request:** a slowloris client never trips them.
- **`O_NOFOLLOW` checks the last path component only:** symlinked directories are followed.
- **No percent-decoding:** `%2e%2e` is a literal file name.
- **Request bodies are not read:** a request with a body may get an RST on close.
- **No keep-alive, TLS or IPv6.**

## What I learned

- **TCP is a byte stream, not messages:** one `recv` is not one request; finding where a request ends is the server's job.
- **`send` can send less than you asked:** partial sends are normal, so every send needs a loop.
- **The kernel does more than I expected:** it completes the TCP handshake on its own; `accept` only hands over a finished connection.
- **Every byte from the network is untrusted:** strict parsing, length-bounded views, and no C-string functions on raw input.
- **Tests can pass for the wrong reason:** breaking the code on purpose is how you find out whether they actually test anything.

*I wrote the server code myself, using Claude for explanations, design discussion and code review; this README was drafted with Claude Code and checked against the source.*
