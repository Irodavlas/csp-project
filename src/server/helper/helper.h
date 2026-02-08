#ifndef HELPER_H
#define HELPER_H

#include <pwd.h>
#include <unistd.h>   // getuid, seteuid, setegid
#include <sys/types.h> // uid_t, gid_t
#include "handler/handlers.h"
#include <semaphore.h>

#define MAX_USERS 20
#define MAX_TRANSFERS 20
#define FILENAME_SIZE 256

typedef struct {
    // define the pipe 
    int socket_fds;
    char rootDir[64];
} Helper;


typedef struct {
    char username[32];
    pid_t handler_pid;
    int is_active; 
} UserEntry;

typedef enum {FREE, PENDING, NOTIFIED, REJECTED} TransferStatus;

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

Helper* CreateHelper(int socket_fd, char* rootDir);
void handleCommands(Helper* helper, int server_fds);
void runHelperLoop(Helper* helper);
int dropPrivilegesTemp(ClientSession *cs);
int regainRoot();
int sandboxUserToRoot(const ClientSession* session, char* rootdir);
int sandboxUserToHisHome(const ClientSession* session);
int CreateSystemUser( const char* rootDir,const char* username, mode_t privileges, char msg[], size_t msgLen);
void initSharedRegistry();
void SharedMemCleanup();
void handleHelperLogin(int server_fd, char* username, helper_response* res);

void handleHelperLs(int server_fd,
                    helper_request_header *hdr,
                    char* path,
                    const char *rootDir,
                    helper_response *res);

void ChangeDirectory(int server_fd,
                     helper_request_header *hdr,
                     char* path,
                     helper_response *res);

void HandlerHelperCreateFile(int server_fd,
                             helper_request_header *hdr,
                             const char* filename,
                             mode_t privileges,
                             int makeDir, 
                             helper_response *res);

void HandleHelperChmod(int server_fd, helper_request_header *hdr, const char* filename, mode_t privileges, helper_response *res);

void HandleHelperDelete(int server_fd, helper_request_header *hdr, const char* path, helper_response *res);
void HandleHelperMove(int server_fd, helper_request_header *hdr, const char* path1, const char* path2, helper_response *res);
void HandleHelperRead(int server_fd, helper_request_header *hdr, const char* path, int offset, helper_response *res);

void HandleHelperDownload(int server_fd, helper_request_header *hdr, const char* path, helper_response *res);
void HandleHelperTransfer(int server_fd, helper_request_header *hdr, const char* root, const char* sender, const char* filename, const char* recv, const char* targetPath, helper_response* res); 
#endif