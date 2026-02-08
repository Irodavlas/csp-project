#ifndef NET_H
#define NET_H

#include <limits.h>
#include <sys/types.h>
#include <stdint.h>   
#include <semaphore.h>

#include "common/utility.h"
#include "handler/handlers.h"



typedef enum {CREATE_USER, LOGIN, CD, LS, CREATE_FILE, CHMOD, DELETE, MOVE, READ, WRITE, DOWNLOAD, UPLOAD, TRANSFER} helper_commands;

typedef enum {FREE, PENDING, NOTIFIED, REJECTED} TransferStatus;

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
    char msg[1256];          
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

typedef struct {
    char username[MAX_USERNAME_LEN];
    pid_t handler_pid;
    int is_active; 
} UserEntry;

typedef struct {
    int id;
    char sender[MAX_USERNAME_LEN];
    char receiver[MAX_USERNAME_LEN];
    char filename[FILENAME_SIZE];
    TransferStatus status; 
} TransferRequest;

typedef struct {
    UserEntry online_users[MAX_USERS];
    TransferRequest pending[MAX_TRANSFERS];
    unsigned int global_id_counter;
    sem_t mux; 
} SharedRegistry;

extern SharedRegistry* registry;

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
int sendProtocolMsg(int fd, msg_type type, uint32_t status, const char* msg);
int sendProtocolMsgLocked(int fd, msg_type type, uint32_t status, const char* msg, int is_bg);
int acquire_socket_lock(int fd);
int release_socket_lock(int fd);
int sendHelperRequest(int helper_fd, helper_commands cmd, int argc, char *argv[], ClientSession *session, helper_response *out);
int sendMessage(int fd, const char* msg);
int createUnixSocket(const char* root_dir, gid_t groupId);
int connectToHelper();
#endif