#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MYPORT "8080"

int main(void) {
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints, *res, *p;
    int sockfd, new_fd, status;

    // address structs
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, MYPORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return EXIT_FAILURE;
    }

    // socket loop
    int yes = 1;
    for (p = res; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
            perror("setsockopt");
            close(sockfd);
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("bind");
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

}