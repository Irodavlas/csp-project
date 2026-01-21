// handles each call such as LOGIN, LS etc as routes
#include "handlers.h"
#include "utils.h"
#include "server.h"
#include <stdio.h>
#include <string.h> // strtok
#include <unistd.h> // write
#include <sys/file.h>
#include <errno.h>
#define BUFFERSIZE 256



typedef void (*cmd_func)(int cliend_fds, int argc, char *argv[], Server* server, ClientSession* sessio); // pointer to function (with name cmd_func) taking int and pointer to char []

typedef struct{
    char* name;
    cmd_func func;
    // consider adding a check for logged in users commands

} command;

command commands[] = {
    {"create_user", handleCreateUser},
    {"login", handleLogin},
    {NULL, NULL}
};

// request to send to helper for priviledges actions 


void handleClient(int client_sfd, char root[], Server* server) {
    printf("Handling client request\n");
    
    ClientSession session;
    memset(&session, 0, sizeof(session));
    session.state = STATE_NOT_LOGGED_IN;
    
    if (sendMessage(client_sfd, "> CONNECTED, log-in\n") < 0) return;
    if (sendMessage(client_sfd, "> ") < 0) return;
    

    ssize_t n;
    while (1) {
        // Tcp doesnt garantuee that the data is sent and received in a single package
        char buffer[BUFFERSIZE];
        ssize_t n = readByteStream(client_sfd, buffer, sizeof(buffer));
        if (n < 0) {
            fprintf(stderr, "[handleClient] Error reading from client\n");
            break;
        }
        if (n == 0) {
            printf("[handleClient] Client closed connection\n");
            break;
        }
        printf("[handleClient] Received command: '%s'\n", buffer);
        dispatchCommands(client_sfd, buffer, server, &session);

        if (sendMessage(client_sfd, "> ") < 0) return;
    }
    
    // use an enum to track user state of user login / user creation will be limited to admins or idk
    // or can assume at login that if user doesnt exists, we can request the client to create it issuing the command 
}
int tokenizeCommand(char* input, char* argv[]) {
    int argc = 0;
    // strtok is not reentrant but with fork it should be safe 
    char* token = strtok(input, " "); // parsing on spaces REMEMBER: strtok returns only first token
    while (token != NULL && argc < MAXARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " "); // call again here to procede with the parsing
    }
    return argc;
}
void dispatchCommands(int client_sfd, char* command, Server* server, ClientSession* session) {
    char* argv[MAXARGS];
    int argc = tokenizeCommand(command, argv);
    ssize_t n;
    if (argc == 0) {
        // should write, prob tell to run --help with all commands explained
        char msg[] = "> problems with the commands? try --help\n";
        if (sendMessage(client_sfd, msg) < 0) return; 
        return;
    }
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name,argv[0]) == 0) {
            commands[i].func(client_sfd, argc, argv, server, session);
            return;
        }
    }
    char msg[] = "> command not found\n";
    if (sendMessage(client_sfd, msg) < 0) return; 
    return;
    
}
void handleCreateUser(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session) {
   // parse username for only strings and then talk with helper, then signal client
    
    if (argc != 3) {
        char msg[] = "> Usage: create_user <username> <permissions>\n";
        write(client_sfd, msg, strlen(msg));
        return;
    }
    
    // parse username and permission then call helper for user setup
    if (!isUsernameValid(argv[1])) {
        sendMessage(client_sfd, "> Invalid username\n");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendMessage(client_sfd, "> Internal error\n");
        return;
    }

    helper_response res;

    int status = sendHelperRequest(
        helper_fd,
        CREATE_USER,
        argc - 1,
        &argv[1],
        &res
    );

    if (status < 0) {
        sendMessage(client_sfd, "> Internal server error\n");
    } else {
        sendMessage(client_sfd, res.msg);
    }
    
    close(helper_fd);


}
void handleLogin(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session) {
    if (session->state == STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User already logged trying to log\n");
        sendMessage(client_sfd, "> Already logged in \n");
        return;
    }
    if (argc != 2) {
        sendMessage(client_sfd, "> Usage: login <username>\n");
        return;
    }
    if (!isUsernameValid(argv[1])) {
        sendMessage(client_sfd, "> Invalid username\n");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendMessage(client_sfd, "> Internal error\n");
        return;
    }
    helper_response res;

    int status = sendHelperRequest(
        helper_fd,
        LOGIN,
        argc - 1,
        &argv[1],
        &res
    );

    if (status < 0) {
        sendMessage(client_sfd, "> Internal server error\n");
    } else {
        sendMessage(client_sfd, res.msg);
    }
    close(helper_fd);
    session->state = STATE_LOGGED_IN;
    session->uid = res.uid;
    session->gid = res.gid;
    strcpy(session->workdir, "/");
    strncpy(session->home, res.home, sizeof(session->home));
    strncpy(session->username, argv[1], sizeof(session->username));

    // set value for login = True 
    // create and update the user session
    // talk to handler chdir, chroot the current user to its directory
    //session struct should live in this process
    
}