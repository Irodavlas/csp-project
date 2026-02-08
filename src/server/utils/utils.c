// moslty for error wrapping and logging


// TCP streams read and writes must be handled with care
// https://incoherency.co.uk/blog/stories/reading-tcp-sockets.html
#include "utils/utils.h"
#include "common/utility.h"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdbool.h> 
#include <ctype.h>

#include <stdint.h>



int lock_file(const char *path, LockType type) {
    int flags = (type == LOCK_EXCLUSIVE) ? O_RDWR : O_RDONLY;
    int fd = open(path, flags);
    if (fd < 0) {
        perror("open for locking");
        return -1;
    }

    struct flock fl;
    fl.l_start = 0;
    fl.l_len = 0; 
    fl.l_whence = SEEK_SET;

    if (type == LOCK_EXCLUSIVE) {
        fl.l_type = F_WRLCK;
    } else {
        fl.l_type = F_RDLCK;
    }

    if (fcntl(fd, F_SETLKW, &fl) < 0) { 
        perror("fcntl lock");
        close(fd);
        return -1;
    }

    return fd; 
}

void unlock_file(int fd) {
    if (fd < 0) return;

    struct flock fl;
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    fcntl(fd, F_SETLK, &fl);
    close(fd);
}
int lock_fd(int fd, LockType type) {
    if (fd < 0) return -1;

    struct flock fl;
    fl.l_start = 0;
    fl.l_len = 0;       // 0 means lock the entire file
    fl.l_whence = SEEK_SET;
    fl.l_type = (type == LOCK_EXCLUSIVE) ? F_WRLCK : F_RDLCK;

    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl lock_fd");
        return -1;
    }

    return 0; 
}
int unlock_fd(int fd) {
    if (fd < 0) return -1;

    struct flock fl;
    fl.l_type = F_UNLCK; 
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLK, &fl) < 0) {
        perror("fcntl unlock_fd");
        return -1;
    }

    return 0;
}

bool isUsernameValid(char* username) {
    if (!username) {
        return false;
    }
    size_t len = strlen(username);
    if (len == 0 || len > MAX_USERNAME_LEN) {
        return false;
    }
    for (char* p = username; *p; p++) {
        if (!isalpha((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}


int createUserDirectory(const char* pathname, uid_t uid, gid_t gid, mode_t mode) {
    printf("mode = %04o\n", mode);
    
    // create dir and set it as absolute path
    if (mkdir(pathname, mode) == -1) {
        if (errno == EEXIST) {
            struct stat st;
            if (stat(pathname, &st) == -1){
                perror("stat");
                return -1;
            }
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "%s exists but is not a directory\n", pathname);
                return -1;
            }
            printf("Directory already exists: %s\n", pathname);
            return 0;
        }
        perror("mkdir");
        return -1;
    } else {
        printf("Directory create with name:%s\n", pathname);
    }
    if (chown(pathname, uid, gid) != 0) {
        perror("chown failed");
        return -1;
    }

    if (chmod(pathname, mode) != 0) {
        perror("chmod failed");
        return -1;
    }
    return 0;
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

int sendProtocolMsgBg(int fd, msg_type type, uint32_t status, const char* msg, int is_bg) {
    msg_header resp;
    resp.type = type;
    resp.status = status;
    resp.is_background = (uint8_t)is_bg;
    resp.payloadLength = (uint32_t)strlen(msg) + 1; 

    if (writeAll(fd, &resp, sizeof(resp)) < 0) return -1;
    return writeAll(fd, msg, resp.payloadLength);
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

int sendProtocolMsgLocked(int fd, msg_type type, uint32_t status, const char* msg, int is_bg) {
    int ret = -1;
    if (acquire_socket_lock(fd) == 0) {
        ret = sendProtocolMsgBg(fd, type, status, msg, is_bg);
        release_socket_lock(fd);
    }
    return ret;
}