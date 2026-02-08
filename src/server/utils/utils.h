#ifndef UTILS_H
#define UTILS_H
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h> 

#define MAXARGS 5
#define PAYLOAD 1024
#define ABS_PATH 1024
#define MAX_USERNAME_LEN 20

#include "common/utility.h"
#include "handler/handlers.h"

int sendMessage(int fd, const char* msg);

bool isUsernameValid(char* username);

int createUserDirectory(const char* pathname, uid_t uid, gid_t gid, mode_t mode);
//int addUserToFile(const char* rootDir, const char* username, const char* homedir, mode_t priviledges, char msg[], size_t msgLen);

typedef enum {CREATE_USER, LOGIN, CD, LS, CREATE_FILE, CHMOD, DELETE, MOVE, READ, WRITE, DOWNLOAD, UPLOAD, TRANSFER} helper_commands;

typedef enum { LOCK_SHARED, LOCK_EXCLUSIVE } LockType;


typedef struct {
    uint32_t cmd;           
    uint32_t argc;         
    uint32_t payload_len;   // Total bytes of all strings (including \0)
    ClientSession session;  
    uint32_t offset; // for read/write
    uint32_t data_len;
} helper_request_header;

typedef struct {
    int32_t status;         // 0 = success, <0 = error
    uint32_t payload_len;   
    char msg[256];          
    uint32_t cmd;
    union {
        struct {
            uid_t uid;
            gid_t gid;
            char home[4096];
        } login;
        struct {
            uint32_t count; 
        } ls;
        struct {
            char cwd[1024];
        } cd;
        struct {
            char buff[4096];
        } read;
    } data;
} helper_response;

int sendHelperRequestRW(int helper_fd,
                        helper_commands cmd,
                        int argc,
                        char *argv[],
                        int offset,
                        ClientSession *session,
                        void *data,
                        uint32_t data_len,
                        helper_response *out);

int sendProtocolMsgBg(int fd, msg_type type, uint32_t status, const char* msg, int is_bg);

#endif