#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

void dump_bytes(const void* p, size_t n) {
    const unsigned char* bytes = (unsigned char *) p;
    for (size_t i = 0; i < n; i++) {
        printf("%02x ", bytes[i]);
    }
    printf("\n");
}

int main(void) {
    struct sockaddr_in addr; 
    memset(&addr, 0, sizeof(addr));
    const char* ip = "127.0.0.1";
    // const char* ip = "999.0.0.1";

    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080); // big endian (correct)
    // addr.sin_port = 8080; // small endian x86/ARM (wrong)
    
    int rv = inet_pton(AF_INET, ip, &(addr.sin_addr));
    if (rv == 0) {
        fprintf(stderr, "%s is not valid IPv4 format\n", ip);
        return 1;
    }
    else if (rv < 0) {
        perror("inet_pton");
        return 1;
    }
    
    char buf[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &addr.sin_addr,
        buf, INET_ADDRSTRLEN) == NULL) {
            perror("inet_ntop");
            return 1;
        }
        
    printf("%s:%d\n", buf, ntohs(addr.sin_port));

    dump_bytes(&addr.sin_port, sizeof(addr.sin_port));
    dump_bytes(&addr.sin_addr, sizeof(addr.sin_addr));

    return 0;
}
