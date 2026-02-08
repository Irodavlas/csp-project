// file used for definitions  

#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h> // for mode_t
#include <pwd.h>
typedef struct { 
    int Port;
    char Root[256]; 
    char Ip[16]; // 15 bytes + 1 for \0
    int sfd;

    // add socket 
} Server;

Server* createServer(char* root, int port, char* ip);
int startServer(Server* server);
int createRootDirectory(const char* pathname, mode_t mode);
int dropPriviledges(struct passwd* pw);
struct passwd* userLookUp();
void setup_sigchld(void);
#endif