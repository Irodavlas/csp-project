// handles each call such as LOGIN, LS etc as routes
#include "core/server.h"
#include "net/net.h"

#ifndef HANDLER_H
#define HANDLER_H

typedef enum { STATE_NOT_LOGGED_IN, STATE_LOGGED_IN } ClientState; // to enforce login and creation

typedef struct {
    ClientState state;
    uid_t uid;
    gid_t gid;
    char username[MAX_USERNAME_LEN];
    char home[ABS_PATH];
    char workdir[ABS_PATH]; //relative to home path 
} ClientSession;



void handleClient(int client_sfd, Server* server);
void dispatchCommands(int client_sfd, msg_header* hdr, char* payload, Server* server, ClientSession* session);
int tokenizeCommand(char* input, char* argv[]);
void handleCreateUser(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleLogin(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleCd(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleLs(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleCreateFile(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleChmod(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleDelete(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleMove(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleRead(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleWrite(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleDownload(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleUpload(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleTransferRequest(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleAcceptTransfer(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
void handleRejectTransfer(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr);
#endif