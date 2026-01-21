// handles each call such as LOGIN, LS etc as routes
#include "server.h"
#include "utils.h"
#ifndef HANDLER_H
#define HANDLER_H

typedef enum { STATE_NOT_LOGGED_IN, STATE_LOGGED_IN } ClientState; // to enforce login and creation

typedef struct {
    ClientState state;
    uid_t uid;
    gid_t gid;
    char username[32];
    char home[ABS_PATH];
    char workdir[ABS_PATH]; //relative to home path 
    // other infos obtained after login 
} ClientSession;

void handleClient(int client_sfd, char root[], Server* server);
void dispatchCommands(int client_sfd, char* command, Server* server, ClientSession* session);
int tokenizeCommand(char* input, char* argv[]);
void handleCreateUser(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session);
void handleLogin(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session);



#endif