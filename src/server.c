
#include "http_parse.h"
#include "http_handler.h" 
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// TODO: Change port with CLI arguments
#define MYPORT "8080"
#define BACKLOG 20

enum recv_status {
    RECV_OK,       
    RECV_CLOSED,   
    RECV_ERROR,    
    RECV_FULL,     
};

// HTTP errors map to individual module errors one-to-many
enum http_error {
    HTTP_400,
    HTTP_414,
    HTTP_431,
    HTTP_501,
    HTTP_505, 
};

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

static const char resp_400[] =
"HTTP/1.1 400 Bad Request\r\n"
"Content-Length: 0\r\n"
"Connection: close\r\n\r\n";

static const char resp_200[] =
"HTTP/1.1 200 OK\r\n"
"Content-Length: 0\r\n"
"Connection: close\r\n\r\n";

static void send_error(int fd, enum http_error e) {
    // TODO: Switch over http errors for perror
}

static enum recv_status recv_delim(int new_fd, char* buf, size_t buf_size, size_t* hdr_len, size_t* final_buf_size) {
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
                *final_buf_size = filled;
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
        size_t header_len, fbufsiz;

        enum recv_status rv = recv_delim(new_fd, buf, sizeof buf, &header_len, &fbufsiz); 

        switch (rv) {
            case RECV_OK: {
                struct http_request req = {0};
                enum parse_status pstatus = parse_request(buf, header_len, &req);
                switch (pstatus) {
                    case PARSE_BAD_REQUEST:
                        fprintf(stderr, "server: %s 400\n", s);
                        if (sendall(new_fd, resp_400, sizeof resp_400 - 1, 0) == -1) {
                            perror("server: sendall");
                        }
                        break;
                    case PARSE_HEADERS_TOO_LARGE:
                        fprintf(stderr, "server: %s 431\n", s);
                        if (sendall(new_fd, resp_431, sizeof resp_431 - 1, 0) == -1) {
                            perror("server: sendall");
                        }
                        break;
                    case PARSE_OK: {
                        enum handler_status hstatus = handle_req(&req);
                        switch (hstatus) {
                            case HANDLER_BAD_VER:
                                fprintf(stderr, "server: %s 505\n", s);
                                if (sendall(new_fd, resp_505, sizeof resp_505 - 1, 0) == -1) {
                                    perror("server: sendall");
                                }
                                break;
                            case HANDLER_NOT_IMPL:
                                fprintf(stderr, "server: %s 501\n", s);
                                if (sendall(new_fd, resp_501, sizeof resp_501 - 1, 0) == -1) {
                                    perror("server: sendall");
                                }
                                break;
                            case HANDLER_BAD_REQ:
                                fprintf(stderr, "server: %s 400\n", s);
                                if (sendall(new_fd, resp_400, sizeof resp_400 - 1, 0) == -1) {
                                    perror("server: sendall");
                                }
                                break;
                            case HANDLER_OK: {
                                char pathbuf[PATH_CAP];
                                enum path_status pstat = target_to_path(req.line.target, req.line.target_len,
                                        pathbuf, sizeof pathbuf);
                                switch (pstat) {
                                    case PATH_BAD_REQUEST:
                                        fprintf(stderr, "server: %s 400 (path)\n", s);
                                        if (sendall(new_fd, resp_400, sizeof resp_400 - 1, 0) == -1) {
                                            perror("server: sendall");
                                        }
                                        break;
                                    case PATH_URI_TOO_LONG:
                                        fprintf(stderr, "server: %s 414\n", s);
                                        if (sendall(new_fd, resp_414, sizeof resp_414 - 1, 0) == -1) {
                                            perror("server: sendall");
                                        }
                                        break;
                                    case PATH_OK:
                                        // TODO: Serve 
                                        fprintf(stderr, "server: %s \"%.*s %.*s\" -> %s 200\n", s,
                                                (int)req.line.method_len, req.line.method,
                                                (int)req.line.target_len, req.line.target, pathbuf);
                                        if (sendall(new_fd, resp_200, sizeof resp_200 - 1, 0) == -1) {
                                            perror("server: sendall");
                                        }
                                        break;
                                }
                                break;
                            }
                        }
                        break;
                    }   
                }      
                break;
            }         
            case RECV_CLOSED:
                fprintf(stderr, "server: %s closed before end of headers\n", s);
                break;
            case RECV_ERROR:
                fprintf(stderr, "server: %s recv error\n", s);
                break;
            case RECV_FULL:
                fprintf(stderr, "server: %s 431\n", s);
                if (sendall(new_fd, resp_431, sizeof resp_431 - 1, 0) == -1) {
                    perror("server: sendall");
                }
                break;
        }        
        close(new_fd);
    }
}
