#include "http_parse.h"
#include "http_handler.h" 
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>      /* open, O_RDONLY, O_CLOEXEC */
#include <sys/stat.h>   /* fstat, struct stat, S_ISREG */

// TODO: Change port with CLI arguments
#define MYPORT "8080"
#define BACKLOG 20
#define CHUNK_SIZE 8192
#define HDR_CAP 256

enum serve_file_status {
    SERVE_OK,
    SERVE_NOT_FOUND,
    SERVE_INTERNAL_ERR,
    SERVE_ABORT,
};

enum recv_status {
    RECV_OK,       
    RECV_CLOSED,   
    RECV_ERROR,    
    RECV_FULL,     
};

// HTTP errors map to individual module errors many-to-one
enum http_error {
    HTTP_400,
    HTTP_404,
    HTTP_500,
    HTTP_414,
    HTTP_431,
    HTTP_501,
    HTTP_505, 
};


static enum recv_status recv_delim(int new_fd, char* buf, size_t buf_size, size_t* hdr_len) {
    size_t filled = 0;
    ssize_t n = 0;

    while (filled < buf_size && (n = recv(new_fd, buf + filled, buf_size - filled, 0)) > 0) {
        filled += n;
        // walk whole buffer and check for \r\n\r\n
        for (size_t i = 0; i + 4 <= filled; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' &&
                    buf[i+2] == '\r' && buf[i+3] == '\n') {
                // Will return idx one past last \n
                *hdr_len = i + 4;
                return RECV_OK;
            }
        }

    }
    if (n == 0) {
        return RECV_CLOSED;
    }

    if (n == -1) {
        perror("server: recv");
        return RECV_ERROR;
    }
    // exited from filled < buf_size
    return RECV_FULL;
}

static int sendall(int sockfd, const void* msg, size_t len, int flags) {
    size_t bytes_total = 0;

    while (bytes_total < len) {
        // bytes_sent can be -1 so need signed size_t -- ssize_t
        ssize_t bytes_sent = send(sockfd, (const char *) msg + bytes_total, len - bytes_total, flags);
        if (bytes_sent == -1) return -1;
        bytes_total += bytes_sent;
    }
    return 0;
}

static enum serve_file_status serve_file(int client_fd, const char* path, enum method m) {
    int file_fd = open(path, O_RDONLY);
    if (file_fd == -1) {
        int err = errno;
        // 404 Error Set - EACCES included to prevent users from finding existing paths without permission
        if (err == EACCES || err == ENOENT || err == ENOTDIR) {
            fprintf(stderr, "server: serve_file: open encountered error %s for %s\n", strerror(err), path);
            return SERVE_NOT_FOUND; 
        }
        else {
            fprintf(stderr, "server: serve_file: open encountered error %s for %s\n", strerror(err), path);
            return SERVE_INTERNAL_ERR;
        }
    }
    struct stat statbuf;
    if (fstat(file_fd, &statbuf) == -1) {
        // no need to split on error return since it's only 500 -- can only be server-side issue
        int err = errno;
        fprintf(stderr, "server: serve_file: fstat encountered error %s for %s\n", strerror(err), path);
        close(file_fd);
        return SERVE_INTERNAL_ERR;
    }

    // if not regular file return not found
    if (!S_ISREG(statbuf.st_mode)) {
        fprintf(stderr, "server: serve_file: %s not a regular file\n", path);
        close(file_fd);
        return SERVE_NOT_FOUND;
    }

    char hdrbuf[HDR_CAP];
    int r = snprintf(hdrbuf, sizeof hdrbuf, "HTTP/1.1 200 OK\r\nContent-Length: %jd\r\nContent-Type: %s\r\nConnection: close\r\n\r\n",
            (intmax_t)statbuf.st_size, "application/octet-stream");

   // check for fail and truncaiton 
    if (r < 0 || (size_t)r >= sizeof hdrbuf) {
        fprintf(stderr, "server: serve_file: snprintf failed (%s)\n", path);
        close(file_fd);
        return SERVE_INTERNAL_ERR;

    }

    if (sendall(client_fd, hdrbuf, (size_t)r, 0) == -1) {
            fprintf(stderr, "server: serve_file: sendall failed on header (%s): %s\n", path, strerror(errno));
            close(file_fd);
            return SERVE_ABORT;
    }

    if (m == METHOD_HEAD) {
        close(file_fd);
        return SERVE_OK;
    }

    char bdbuf[CHUNK_SIZE];
    ssize_t n;
    while((n = read(file_fd, bdbuf, sizeof bdbuf)) > 0) {
        if(sendall(client_fd, bdbuf, (size_t)n, 0) == -1) {
            fprintf(stderr, "server: serve_file: send failed mid-body (%s): %s\n",
                    path, strerror(errno));
            close(file_fd);
            return SERVE_ABORT;
        }
    }

    if (n == -1) {
        fprintf(stderr, "server: serve_file: read failed mid-body (%s): %s\n",
                path, strerror(errno));
        close(file_fd);
        return SERVE_ABORT;
    }

    close(file_fd);
    return SERVE_OK;
}


static void send_error(int fd, enum http_error e) {
   
    static const char resp_500[] =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_501[] =
        "HTTP/1.1 501 Not Implemented\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_505[] =
        "HTTP/1.1 505 HTTP Version Not Supported\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_431[] =
        "HTTP/1.1 431 Request Header Fields Too Large\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_414[] =
        "HTTP/1.1 414 URI Too Long\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_404[] =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    static const char resp_400[] =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

    const char *msg = NULL;
    size_t len = 0;

    switch (e) {
        case HTTP_400: msg = resp_400; len = sizeof resp_400 - 1; break;
        case HTTP_404: msg = resp_404; len = sizeof resp_404 - 1; break;
        case HTTP_414: msg = resp_414; len = sizeof resp_414 - 1; break;
        case HTTP_431: msg = resp_431; len = sizeof resp_431 - 1; break;
        case HTTP_500: msg = resp_500; len = sizeof resp_500 - 1; break;
        case HTTP_505: msg = resp_505; len = sizeof resp_505 - 1; break;
        case HTTP_501: msg = resp_501; len = sizeof resp_501 - 1; break;
    }

    if (msg == NULL) {
        fprintf(stderr, "http: send_error: unknown %d\n", (int)e); 
        return; 
    }

    if (sendall(fd, msg, len, 0) == -1){
        perror("server: sendall");
    }
}

// Tests for errors sequentially parse->handler->path and calls send_error for perror and logs on the server console with fprintf 
static void respond(int fd, char* buf, size_t len) {
    struct http_request req = {0};
    enum parse_status pstatus = parse_request(buf, len, &req);
    switch (pstatus) {
        case PARSE_BAD_REQUEST:
            send_error(fd, HTTP_400);
            return;
        case PARSE_HEADERS_TOO_LARGE:
            send_error(fd, HTTP_431);
            return;
        case PARSE_OK:
            break;
    }

    enum method m;
    enum handler_status hstatus = handle_req(&req, &m);
    switch (hstatus) {
        case HANDLER_BAD_VER:
            fprintf(stderr, "http: respond: unsupported version: %.*s\n",
                    (int)req.line.version_len, req.line.version);
            send_error(fd, HTTP_505);
            return;
        case HANDLER_NOT_IMPL:
            fprintf(stderr, "http: respond: method not implemented: %.*s\n",
                    (int)req.line.method_len, req.line.method);
            send_error(fd, HTTP_501);
            return;
        case HANDLER_BAD_REQ:
            fprintf(stderr, "http: respond: missing or duplicate Host\n");
            send_error(fd, HTTP_400);
            return;
        case HANDLER_OK:
            break;
    }

    char pathbuf[PATH_CAP];
    enum path_status pthstatus = target_to_path(req.line.target, req.line.target_len, pathbuf, sizeof pathbuf);
    switch (pthstatus) {
        case PATH_BAD_REQUEST:
            fprintf(stderr, "http: respond: bad target: %.*s\n",
                    (int)req.line.target_len, req.line.target);
            send_error(fd, HTTP_400);
            return;
        case PATH_URI_TOO_LONG:
            fprintf(stderr, "http: respond: target too long (%zu bytes)\n",
                    req.line.target_len);
            send_error(fd, HTTP_414);
            return;
        case PATH_OK:
            break;
    }

    enum serve_file_status srvstatus = serve_file(fd, pathbuf, m);
    switch (srvstatus) {
        case SERVE_NOT_FOUND:
            send_error(fd, HTTP_404);
            return;
        case SERVE_INTERNAL_ERR:
            send_error(fd, HTTP_500);
            return;
        case SERVE_ABORT:
            return;
        case SERVE_OK:
            break;
    }

    fprintf(stderr, "http: respond: %.*s %.*s -> %s 200\n",
          (int)req.line.method_len, req.line.method,
          (int)req.line.target_len, req.line.target, pathbuf);
}

static void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
    struct addrinfo hints, *res, *p;
    int sockfd, status;

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) { // SIGPIPE guard
        perror("server: signal");
        return EXIT_FAILURE;
    } 

    // address structs
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(NULL, MYPORT, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "server: getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // socket loop
    int yes = 1;
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
            perror("server: setsockopt");
            close(sockfd);
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("server: bind");
            close(sockfd);
            continue;
        }
        break;
    }    
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind to an address\n");
        freeaddrinfo(res);
        return EXIT_FAILURE;
    }
    freeaddrinfo(res);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("server: listen");
        close(sockfd);
        return EXIT_FAILURE;
    }

    for (;;) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof their_addr;
        char s[INET6_ADDRSTRLEN] = "?";


        int new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &addr_size);
        if (new_fd == -1) {
            perror("server: accept");
            continue;
        }

        // print ip connected to server
        if (inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
                    s, sizeof s) == NULL) {
            perror("server: inet_ntop");
        }

        char buf[4096];
        size_t header_len;

        enum recv_status rv = recv_delim(new_fd, buf, sizeof buf, &header_len); 

        switch (rv) {
            case RECV_OK: 
                respond(new_fd, buf, header_len);
                break;
            case RECV_CLOSED:
                fprintf(stderr, "server: %s closed before end of headers\n", s);
                break;
            case RECV_ERROR:
                fprintf(stderr, "server: %s recv error\n", s);
                break;
            case RECV_FULL:
                fprintf(stderr, "server: %s headers exceed buffer\n", s);
                send_error(new_fd, HTTP_431);
                break;

        }
        close(new_fd);
    }
}
