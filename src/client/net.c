
#include "net.h"

int connectToServer(char *ip, int port) {
    int client_fd;
    int status;
    struct sockaddr_in server_addr;
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socker");
        printf("Socket creation error \n");
        return -1;
    }
    server_addr.sin_family = AF_INET;
    /*
        The htons() function converts the unsigned short integer hostshort 
        from host byte order to network byte order.
    */
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", ip);
        return -1;
    }
    server_addr.sin_port = htons(port);

    if ((status = connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0) {
        perror("connect");
        return -1;
    }

    return client_fd;

}