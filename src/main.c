#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

// The normal procedure for parsing an HTTP message is to read the start-line into a structure, 
// read each header field line into a hash table by field name until the empty line, 
// and then use the parsed data to determine if a message body is expected. 
// If a message body has been indicated, then it is read as a stream until an amount of octets equal to the message body length is read or the connection is closed.

typedef struct header{
    const char* name;
    size_t name_len;
    const char* value;
    size_t value_len;
} header;

typedef struct http_request {
    header headers[32]; // max of 32 headers
    size_t count;
} http_request;


#define MYPORT "8080"
#define BACKLOG 20

enum parse_status {
    PARSE_OK,
    PARSE_SYN,
    PARSE_TMHEAD,
};

enum recv_status {
    RECV_OK,       
    RECV_CLOSED,   
    RECV_ERROR,    
    RECV_FULL,     
};


static bool is_ctl(unsigned char c) {
    // function expects unsigned char for x86 same reason as istchar
    return line[j] != '\t' && (line[j] < 0x20 || line[j] == 0x7F);
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

static int parse_header_line(const char* line, size_t len, header *out) {
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


        
static enum recv_status recv_delim(int new_fd, char* buf, size_t buf_size, size_t* delim_idx, size_t* final_buf_size) {
    size_t filled = 0;
    ssize_t n = 0;

    while (filled < buf_size && (n = recv(new_fd, buf + filled, buf_size - filled, 0)) > 0) {
        filled += n;
        // walk whole buffer and check for \r\n\r\n
        for (size_t i = 0; i + 4 <= filled; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' &&
                    buf[i+2] == '\r' && buf[i+3] == '\n') {
                // Will return idx one past last \n
                *delim_idx = i + 4;
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

int sendall(int sockfd, const void* msg, size_t len, int flags) {
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
        char s[INET6_ADDRSTRLEN];


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
        else {
            printf("connection from %s\n", s);
        }

        char buf[4096];
        size_t delim_idx, fbufsiz;

        enum recv_status rv = recv_delim(new_fd, buf, sizeof buf, &delim_idx, &fbufsiz); 

        switch (rv) {
            case RECV_OK:
                printf("header idx end: %zu\t buffer size: %zu\n", delim_idx, fbufsiz);
                //parse here
                break;
            case RECV_CLOSED:
                fprintf(stderr, "server: headers: Connection closed without valid HTTP message.\n");
                break;
            case RECV_ERROR:
                break;
            case RECV_FULL:
                // TODO send 431
                fprintf(stderr, "server: headers: Buffer limit reached. HTTP could not be parsed.\n");
                break;
        }
        close(new_fd);
    }
}
