#ifndef HELPER_H
#define HELPER_H

#include <pwd.h>
#include <unistd.h>   // getuid, seteuid, setegid
#include <sys/types.h> // uid_t, gid_t

typedef struct {
    // define the pipe 
    int socket_fds;
    char rootDir[64];
} Helper;


Helper* CreateHelper(int socket_fd, char* rootDir);
void handleCommands(Helper* helper, int server_fds);
void runHelperLoop(Helper* helper);
int dropPriviledgesTemp(struct passwd* pw);
int regainRoot();
int CreateSystemUser( const char* rootDir,const char* username, mode_t privileges, char msg[], size_t msgLen);
#endif