#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MYPORT "8080"
#define BACKLOG 20

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

void *get_in_addr(struct sockaddr *sa) {
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
        
        ssize_t n;
        char buf[4096];

        while ((n = recv(new_fd, buf, sizeof buf, 0)) > 0) {
            if (sendall(new_fd, buf, n, 0) == -1) {
                perror("server: send");
                break;
            }
        }

        if (n == -1) {
            perror("server: recv");
        }
        close(new_fd);


    }
}