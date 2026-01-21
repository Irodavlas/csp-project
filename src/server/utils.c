// moslty for error wrapping and logging


// TCP streams read and writes must be handled with care
// https://incoherency.co.uk/blog/stories/reading-tcp-sockets.html
#include "utils.h"

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

#define HELPER_SOCK_REL "/tmp/helper.sock"
#define MAX_USERNAME_LEN 20


ssize_t readByteStream(int client_fd, char* buffer, size_t size) {
    /* 
    here we could have also defined a static buffer to make it persistent and return a pointer to it.
    static char buffer[BUFFERSIZE]; 
    
    */
    int len = 0;

    while (len < size - 1) {
        // difference between size_T and ssize_t is long unsigned and long, so no errors on size_t
        if (len == size - 1) {
            errno = EMSGSIZE;
            printf("Message was too big to fit inside buffer\n");
            return -1;
        }
        ssize_t n = read(client_fd, &buffer[len], 1);
        
        if (n < 0) {
            if (errno == EINTR)
                continue;              // retry on signal
            perror("read error");
            return -1;                 
        }
        if (n == 0) {                  // client closed
            printf("Client disconected\n");
            return 0;                
        }
        if (buffer[len] == '\n') {
            break;
        }
        len++;
    }
    buffer[len] = '\0';
    return (ssize_t)len;

}


ssize_t writeByteStream(int client_fds, const char* response, size_t len) {
    int sent = 0;
    const char* p = response;
    while (sent < len) {
        // write will try to write up to (len - sent), so up to all msg.
        // if it writes less, then n is returned and the next iteration will take care of it.
        ssize_t n = write(client_fds, p + sent, len - sent); 

        if (n < 0 ) {
            if (errno == EINTR)
                continue;   
            perror("write");
            return -1;
        }
        if (n == 0) {
            printf("Client disconected\n");
            return sent;
        }
        sent += n;

    }
    return sent;
}

int sendMessage(int fd, const char* msg) {
    ssize_t n = writeByteStream(fd, msg, strlen(msg));
    if (n <= 0) {
        fprintf(stderr, "Error writing to client\n");
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
int sendHelperRequest(
    int helper_fd,
    helper_commands cmd,
    int argc,
    char *argv[],
    helper_response *out
) {
    helper_request req;
    memset(&req, 0, sizeof(req));

    req.cmd  = cmd;
    req.argc = argc;

    char *dst = req.payload;
    size_t remaining = sizeof(req.payload);

    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1; // include '\0'

        if (len > remaining) {
            return -1;  
        }

        memcpy(dst, argv[i], len);
        dst += len;
        remaining -= len;
    }    
    ssize_t w = write(helper_fd, &req, sizeof(req));
    if (w != sizeof(req)) {
        return -1;
    }

    ssize_t r = read(helper_fd, out, sizeof(*out));
    if (r != sizeof(*out)) {
        return -1;
    }

    return out->status;
}


/*
int addUserToFile(const char* rootDir, const char* username, const char* homedir, mode_t priviledges, char msg[], size_t msgLen) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", rootDir, USERS_FILE);

    FILE* f = fopen(path, "a");
    if (!f) {
        snprintf(msg, msgLen, "> internal server error");
        perror("fopen users.txt");
        return -1;
    }
    if (flock(fileno(f), LOCK_EX) < 0) {
        snprintf(msg, msgLen, "> internal server error");
        perror("flock");
        fclose(f);
        return -1;
    }
    // write username:home:permBits to file 
    if (fprintf(f, "%s:%s:%o\n", username, homedir, priviledges) < 0) {
        snprintf(msg, msgLen, "> internal server error");
        perror("fprintf");
        flock(fileno(f), LOCK_UN);
        fclose(f);
        return -1;
    }

    fflush(f);
    flock(fileno(f), LOCK_UN);
    fclose(f);

    return 0;
}
*/