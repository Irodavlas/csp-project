

#include <stdio.h>        // perror, snprintf
#include <string.h>       // memset, strncpy
#include <unistd.h>       // close, unlink, chown
#include <sys/types.h>    // gid_t
#include <sys/socket.h>   // socket, bind, connect, listen
#include <sys/un.h>       // sockaddr_un
#include <sys/stat.h>     // chmod

#define HELPER_SOCK_REL "/tmp/helper.sock"

int createUnixSocket(const char* root_dir, gid_t groupId) {
    int sockfd;
    struct sockaddr_un addr;

    char socket_path[60];
    snprintf(socket_path, sizeof(socket_path), "%s/%s", root_dir, HELPER_SOCK_REL);

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    unlink(addr.sun_path); // remove old socket if exists
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    if (chown(addr.sun_path, -1, groupId) < 0) {
        perror("chown socket");
        close(sockfd);
        return -1;
    }

    if (chmod(addr.sun_path, 0660) < 0) {
        perror("chmod socket");
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int connectToHelper() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HELPER_SOCK_REL, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd; 
}