#ifndef UTILITY_H
#define UTILITY_H
#include <stdio.h>
#include <stdint.h>

#define ABS_PATH 1024
#define MAXARGS 5
#define PAYLOAD 1024
#define MAX_USERNAME_LEN 20

#define MAX_USERS 20
#define MAX_TRANSFERS 20
#define FILENAME_SIZE 256

#define SOCKT_MAX 128

typedef enum { LOCK_SHARED, LOCK_EXCLUSIVE } LockType;
typedef enum {TEXT, LSRES, CMDREQ, READCMD, WRITECMD, BACKGROUND, DOWNLOAD_RES, UPLOAD_RES} msg_type;

int validate_ipv4(const char* ip);
int validate_port(int port);
ssize_t readByteStream(int client_fd, char* buffer, size_t size);
ssize_t writeByteStream(int client_fds, const char* response, size_t len);
ssize_t writeAll(int fd, const void *buf, size_t len);
ssize_t readAll(int fd, void *buf, size_t len) ;
int sendMessage(int fd, const char* msg);
int lock_file(const char *path, LockType type);
void unlock_file(int fd);
int lock_fd(int fd, LockType type);
int unlock_fd(int fd);


typedef struct {
    msg_type type;
    uint32_t status;
    uint32_t payloadLength;
    uint8_t  is_background;
} msg_header;

typedef struct {
    char name[56];
    char perms[11];
    off_t size;
} FileEntry;

typedef struct {
    int port;
    char dest_path[256];
    int is_bg;
} bg_download_args;

#endif