#ifndef UTILS_H
#define UTILS_H
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h> 

#define MAXARGS 5
#define PAYLOAD 1024
#define ABS_PATH 1024
//#define USERS_FILE "users.txt"

ssize_t readByteStream(int client_fd, char* buffer, size_t size);
ssize_t writeByteStream(int client_fds, const char* response, size_t len);
int sendMessage(int fd, const char* msg);
int createUnixSocket(const char* root_dir, gid_t groupId);
bool isUsernameValid(char* username);
int connectToHelper();
int createUserDirectory(const char* pathname, uid_t uid, gid_t gid, mode_t mode);
//int addUserToFile(const char* rootDir, const char* username, const char* homedir, mode_t priviledges, char msg[], size_t msgLen);

typedef enum {CREATE_USER, LOGIN} helper_commands;

typedef struct {
    int status;
    uid_t uid;
    gid_t gid;
    char home[ABS_PATH];
    char msg[50];
} helper_response;

int sendHelperRequest(
    int helper_fd,
    helper_commands cmd,
    int argc,
    char *argv[],
    helper_response *out
);


typedef struct {
    helper_commands cmd;
    int argc;
    char payload[PAYLOAD];
} helper_request;



#endif