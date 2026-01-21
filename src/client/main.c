#include <stdio.h>
#include <stdlib.h>

#include "common/utility.h"

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8080

int main(int argc, char* argv[]) {
    if (argc > 3){ // check first arg  
        printf("Usage: %s [IP] [Port]\n", argv[0]);
        return -1;
    } 
    char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    if (argc >= 2 && argv[1][0] != '\0') {
        ip = argv[1];
        if (!validate_ipv4(ip)) {
            printf("IP: %s not valid\n", ip);
            return -1;
        }
        ip = argv[1];
    }
    if (argc >= 3 && argv[2][0] != '\0') {
        port = atoi(argv[2]);
    }
    printf("IP: %s, Port:%d\n", ip, port);
    return 0;

}