

#include <stdio.h>        // perror, snprintf
#include <string.h>       // memset, strncpy, strlen
#include <unistd.h>       // close, unlink, chown
#include <fcntl.h>        // fcntl, F_SETLKW 
#include <sys/types.h>    
#include <sys/socket.h>   
#include <sys/un.h>       
#include <sys/stat.h>

#include "net/net.h"

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

int sendProtocolMsgLocked(int fd, msg_type type, uint32_t status, const char* msg, int is_bg) {
    int ret = -1;
    if (acquire_socket_lock(fd) == 0) {
        ret = sendProtocolMsgBg(fd, type, status, msg, is_bg);
        release_socket_lock(fd);
    }
    return ret;
}
int sendProtocolMsgBg(int fd, msg_type type, uint32_t status, const char* msg, int is_bg) {
    msg_header resp;
    resp.type = type;
    resp.status = status;
    resp.is_background = (uint8_t)is_bg;
    resp.payloadLength = (uint32_t)strlen(msg) + 1; 

    if (writeAll(fd, &resp, sizeof(resp)) < 0) return -1;
    return writeAll(fd, msg, resp.payloadLength);
}

int sendHelperRequestRW(int helper_fd,
                        helper_commands cmd,
                        int argc,
                        char *argv[],
                        int offset,
                        ClientSession *session,
                        void *data,
                        uint32_t data_len,
                        helper_response *out)
{
    uint32_t p_len = 0;
    for (int i = 0; i < argc; i++)
        p_len += strlen(argv[i]) + 1;

    helper_request_header req_hdr = {
        .cmd = cmd,
        .argc = argc,
        .payload_len = p_len,
        .offset = offset,
        .data_len = data_len
    };

    if (session) req_hdr.session = *session;

    if (writeAll(helper_fd, &req_hdr, sizeof(req_hdr)) < 0) return -1;

    for (int i = 0; i < argc; i++)
        if (writeAll(helper_fd, argv[i], strlen(argv[i]) + 1) < 0)
            return -1;

    if (data_len > 0 && data) {
        if (writeAll(helper_fd, data, data_len) < 0)
            return -1;
    }
    if (readAll(helper_fd, out, sizeof(helper_response)) <= 0)
        return -1;

    return out->status;
}

int sendHelperRequest(int helper_fd, helper_commands cmd, int argc, char *argv[], ClientSession *session, helper_response *out) {
    uint32_t p_len = 0;
    for (int i = 0; i < argc; i++) {
        p_len += strlen(argv[i]) + 1;
    }

    helper_request_header req_hdr = {
        .cmd = cmd,
        .argc = argc,
        .payload_len = p_len,
        .offset = 0,
    };
    if (session) req_hdr.session = *session;

    if (writeAll(helper_fd, &req_hdr, sizeof(req_hdr)) < 0) return -1;

    for (int i = 0; i < argc; i++) {
        if (writeAll(helper_fd, argv[i], strlen(argv[i]) + 1) < 0) return -1;
    }
    if (readAll(helper_fd, out, sizeof(helper_response)) <= 0) return -1;

    return out->status;
}


int sendProtocolMsg(int fd, msg_type type, uint32_t status, const char* msg) {
    msg_header resp;
    resp.type = type;
    resp.status = status;
    resp.is_background = 0;
    resp.payloadLength = strlen(msg) + 1; 

    if (writeAll(fd, &resp, sizeof(resp)) < 0) return -1;
    if (resp.payloadLength > 0) {
        if (writeAll(fd, msg, resp.payloadLength) < 0) return -1;
    }
    return 0;
}
int acquire_socket_lock(int fd) {
    struct flock fl = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("acquire_socket_lock");
        return -1;
    }
    return 0;
}
int release_socket_lock(int fd) {
    struct flock fl = {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        perror("release_socket_lock");
        return -1;
    }
    return 0;
}