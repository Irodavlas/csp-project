// handles each call such as LOGIN, LS etc as routes
#define _GNU_SOURCE

#include "handler/handlers.h"
#include "utils/utils.h"
#include "common/utility.h"
#include "server/server.h"
#include "helper/helper.h"


#include "net/net.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h> // strtok
#include <unistd.h> // write
#include <sys/file.h>
#include <errno.h>
#include <semaphore.h>

#include <stdlib.h>  // For malloc() and free()
#include <arpa/inet.h> // For htonl() and ntohl()
#include <signal.h>


#define BUFFERSIZE 256



typedef void (*cmd_func)(int cliend_fds, int argc, char *argv[], Server* server, ClientSession* session, msg_header* hdr); // pointer to function (with name cmd_func) taking int and pointer to char []

typedef struct{
    char* name;
    cmd_func func;
    // consider adding a check for logged in users commands

} command;

command commands[] = {
    {"create_user", handleCreateUser},
    {"login", handleLogin},
    {"ls", handleLs},
    {"cd", handleCd},
    {"create", handleCreateFile},
    {"chmod", handleChmod},
    {"delete", handleDelete},
    {"move", handleMove},
    {"read", handleRead},
    {"write", handleWrite},
    {"download", handleDownload},
    {"upload", handleUpload},
    {"transfer_request", handleTransferRequest},
    {"accept", handleAcceptTransfer},
    {"reject", handleRejectTransfer},
    {NULL, NULL}
};

volatile sig_atomic_t transfer_signal_received = 0;

void handle_sigusr1(int sig) {
    transfer_signal_received = 1;
}
void setup_signal_handling() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("[Signal Setup] sigaction failed");
        exit(1);
    }
}

    
void check_for_notifications(int client_fds, ClientSession* session) {
    if (session->state != STATE_LOGGED_IN) return;

    TransferRequest to_notify[MAX_TRANSFERS];
    int is_rejection[MAX_TRANSFERS]; // 1 rej, 0 pend
    int count = 0;
    // we create a local snapshot of shared mem
    // otw we would need to keep sem lock while sending to client (unsafe)
    sem_wait(&registry->mux);
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        // sent this process a file 
        if (registry->pending[i].status == PENDING && 
            strcmp(registry->pending[i].receiver, session->username) == 0) {
            
            to_notify[count] = registry->pending[i];
            is_rejection[count] = 0;
            count++;
            
            registry->pending[i].status = NOTIFIED; 
        }
        // send rejected 
        else if (registry->pending[i].status == REJECTED && 
                 strcmp(registry->pending[i].sender, session->username) == 0) {
            
            to_notify[count] = registry->pending[i];
            is_rejection[count] = 1;
            count++;
            
            memset(&registry->pending[i], 0, sizeof(TransferRequest));
            registry->pending[i].status = FREE;
        }
    }
    sem_post(&registry->mux);

    for (int i = 0; i < count; i++) {
        char msg[512];
        if (is_rejection[i]) {
            snprintf(msg, sizeof(msg), 
                "[NOTIFICATION] User %s REJECTED your transfer: %s", 
                to_notify[i].receiver, to_notify[i].filename);
        } else {
            snprintf(msg, sizeof(msg), 
                "[NOTIFICATION] User %s sent a file: %s (ID: %d)\n"
                "   > Accept: accept <dir> %d\n"
                "   > Reject: reject %d", 
                to_notify[i].sender, to_notify[i].filename, 
                to_notify[i].id, to_notify[i].id, to_notify[i].id);
        }
        
        sendProtocolMsgBg(client_fds, TEXT, 0, msg, 1);
    }
}
void cleanup_user_requests(const char* username) {
    sem_wait(&registry->mux);
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (registry->pending[i].status != FREE && 
            strcmp(registry->pending[i].receiver, username) == 0) {
            
            registry->pending[i].status = FREE;
            registry->pending[i].id = 0;
            memset(registry->pending[i].sender, 0, 32);
        }
    }
    sem_post(&registry->mux);
}
void handleClient(int client_sfd, Server* server) {
    printf("Handling client request\n");

    setup_signal_handling();
    
    ClientSession session;
    memset(&session, 0, sizeof(session));
    session.state = STATE_NOT_LOGGED_IN;

   
    while (1) {
        if (transfer_signal_received) {
            check_for_notifications(client_sfd, &session);
            transfer_signal_received = 0;
        }
        msg_header hdr;
        ssize_t n = readAll(client_sfd, &hdr, sizeof(hdr));
        if (n < 0) {
            if (errno == EINTR) {
                // if read was intrpt by signal then restart the loop and check for nots
                continue;
            }
            printf("[handleClient] Error reading from client\n");
            break;
        }
        if (n == 0) {
            printf("[handleClient] Client closed connection\n");
            break;
        }
        char *payload = NULL;
        if (hdr.payloadLength > 0) {
            payload = malloc(hdr.payloadLength + 1); // +1 for safety null terminator
            if (readAll(client_sfd, payload, hdr.payloadLength) <= 0) {
                free(payload);
                break;
            }
            payload[hdr.payloadLength] = '\0'; 
        }
        printf("[handleClient] Received Type: %d, Size: %u\n", hdr.type, hdr.payloadLength);
        dispatchCommands(client_sfd, &hdr, payload, server, &session);
        if (payload) free(payload);
    }

    // remove online user from registry upon disconection
    if (session.state == STATE_LOGGED_IN) {

        // clean shmem entries to avoid freezing server
        cleanup_user_requests(session.username);

        printf("[handleClient] Cleaning up registry for %s\n", session.username);
        sem_wait(&registry->mux);
        for(int i=0; i<MAX_USERS; i++) {
            if(registry->online_users[i].handler_pid == getpid()) {
                registry->online_users[i].is_active = 0;
                break;
            }
        }
        sem_post(&registry->mux);
    }

    close(client_sfd);
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
void dispatchCommands(int client_sfd, msg_header* hdr, char* payload, Server* server, ClientSession* session) {
    char* argv[MAXARGS];
    int argc = tokenizeCommand(payload, argv);
    if (argc == 0) {
        sendProtocolMsg(client_sfd, TEXT, 0, "Problems with command? add args");
        return;
    }

    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name,argv[0]) == 0) {
            commands[i].func(client_sfd, argc, argv, server, session, hdr);
            return;
        }
    }
    sendProtocolMsg(client_sfd, TEXT, -1, "Command not found");
    
}
void handleCreateUser(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
   // parse username for only strings and then talk with helper, then signal client
    if (argc != 3) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: create_user <username> <permissions>");
        return;
    }
    if (!isUsernameValid(argv[1])) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Invalid username");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }

    helper_response res;
    int status = sendHelperRequest(helper_fd, CREATE_USER, argc - 1, &argv[1], NULL, &res);
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    
    close(helper_fd);
}

void handleLogin(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state == STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User already logged trying to log\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Already logged in");
        return;
    }
    if (argc != 2) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: login <username>");
        return;
    }
    if (!isUsernameValid(argv[1])) {
        sendProtocolMsg(client_sfd, TEXT, -1,  "Invalid username");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error");
        return;
    }
    helper_response res;

    int status = sendHelperRequest(
        helper_fd,
        LOGIN,
        argc - 1,
        &argv[1],
        NULL,
        &res
    );
    if (status == 0) {
        
        session->state = STATE_LOGGED_IN;
        session->uid = res.data.login.uid;
        session->gid = res.data.login.gid;
        strcpy(session->workdir, "/");
        strncpy(session->home, res.data.login.home, sizeof(session->home));
        strncpy(session->username, argv[1], sizeof(session->username));
        
        // registry insert of new loggin user
        if (registry != NULL) {
            printf("[handleLogin] Inserting entry into registry for %s\n", session->username);
            sem_wait(&registry->mux);
            for (int i = 0; i < MAX_USERS; i++) {
                if (!registry->online_users[i].is_active) {
                    strncpy(registry->online_users[i].username, argv[1], MAX_USERNAME_LEN);
                    registry->online_users[i].handler_pid = getpid();
                    registry->online_users[i].is_active = 1;
                    break;
                }
            }
            sem_post(&registry->mux);
        }
        sendProtocolMsg(client_sfd, TEXT, 0, "Login successful");
    } else {
        sendProtocolMsg(client_sfd, TEXT, -1, res.msg);
    }
    close(helper_fd);
}

void handleCd(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attempting Cd command  without loggin in\n");
        sendProtocolMsg(client_sfd, TEXT, -1, "Log in first");
        return;
    }
    if (argc != 2) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: cd <path>");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error");
        return;
    }
    helper_response res;

    int status = sendHelperRequest(
        helper_fd,
        CD,
        argc - 1,
        &argv[1],
        session,
        &res
    );

    if (status == 0) {
        printf("current workdir:%s\n", res.data.cd.cwd);
        strncpy(session->workdir, res.data.cd.cwd, sizeof(session->workdir));
        snprintf(res.msg, sizeof(res.msg),
                 "Current workDir: %s", session->workdir);
        sendProtocolMsg(client_sfd, TEXT, 0, res.msg);
    } else {
        sendProtocolMsg(client_sfd, TEXT, -1, res.msg);
    }
    close(helper_fd);

}

void handleLs(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attempting Ls command  without loggin in\n");
        sendProtocolMsg(client_sfd, TEXT, -1, "Log in first");
        return;
    }
    if (argc != 2) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: ls <path>");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal server error: Helper unreachable");
        return;
    }
    helper_response res;
    int status = sendHelperRequest(helper_fd, LS, argc - 1, &argv[1], session, &res);
    if (status != 0) {
        fprintf(stderr, "%s\n", res.msg);
        sendProtocolMsg(client_sfd, TEXT, -1, res.msg);
        close(helper_fd);
        return;
    }
    else if (res.payload_len > 0) {
        void* entries = malloc(res.payload_len);
        if (!entries) {
            sendProtocolMsg(client_sfd, TEXT, -1, "Server memory error");
            close(helper_fd);
            return;
        }
        if (readAll(helper_fd, entries, res.payload_len) == res.payload_len) {
            msg_header client_hdr = {
                .type = LSRES,
                .status = 0,
                .payloadLength = res.payload_len
            };
            
            writeAll(client_sfd, &client_hdr, sizeof(client_hdr));
            writeAll(client_sfd, entries, res.payload_len);
        } else {
            sendProtocolMsg(client_sfd, TEXT, -1, "Failed to read data from Helper");
        }
        free(entries);
    } else {
        sendProtocolMsg(client_sfd, TEXT, 0, "Directory is empty");
    }
    close(helper_fd);
}

/*
• create <path> <permissions (in octal)>: creates an empty le in the location <path> with permissions
<permissions>. The -d option creates a directory.
*/
void handleCreateFile(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attemptin to create file before login\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Log in first");
        return;
    }
    if (argc < 3 || argc > 4) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: create <path> <permissions> [-d]");
        return;
    }
    int makeDir = 0;

    if (argc == 4) {
        if (strcmp(argv[3], "-d") == 0) {
            makeDir = 1;
        } else {
            sendProtocolMsg(client_sfd, TEXT, -1, "Unknown option. Only -d supported");
            return;
        }
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }
    helper_response res;
    int status = sendHelperRequest(helper_fd, CREATE_FILE, argc - 1, &argv[1], session, &res);
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    
    close(helper_fd);
}

// chmod <path> <permissions (in octal)>: Set the <path> le permissions to <permissions>
void handleChmod(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attemptin to change permissions before login\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Log in first");
        return;
    }
    if (argc != 3) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: chmod <path> <permissions>");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }
    helper_response res;
    int status = sendHelperRequest(helper_fd, CHMOD, argc - 1, &argv[1], session, &res);
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    
    close(helper_fd);
}

void handleDelete(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attemptin to delete before login\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Log in first");
        return;
    }
    if (argc != 2) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: delete <path>");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }
    helper_response res;
    int status = sendHelperRequest(helper_fd, DELETE, argc - 1, &argv[1], session, &res);
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    
    close(helper_fd);
}

void handleMove(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attemptin to move before login\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Log in first");
        return;
    }
    if (argc != 3) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: move <path1> <path2>");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }
    helper_response res;
    int status = sendHelperRequest(helper_fd, MOVE, argc - 1, &argv[1], session, &res);
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    
    close(helper_fd);
}

/*
read <path>: Sends the content of <path> to the client who will print it in stdout. It is possible
to specify the -offset=<num> option which will force the sending from the <num> byte of the
file. Example: read -o set=10 <path>: Send the file starting from the offset byte 10.
*/
void handleRead(int client_sfd, int argc, char* argv[], Server* Server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        fprintf(stderr, "[handleClient] User attemptin to read file before login\n");
        sendProtocolMsg(client_sfd, TEXT, 0, "Log in first");
        return;
    }
    int offset = 0;
    char *path = NULL;

    if (argc == 3) {
        const char *prefix = "-offset=";
        if (strncmp(argv[1], prefix, strlen(prefix)) == 0) {
            offset = atoi(argv[1] + strlen(prefix));
            path = argv[2];
        } else {
            sendProtocolMsg(client_sfd, TEXT, -1, "Usage: read -offset=N <path>");
            return;
        }
    } 
    
    else if (argc == 2) {
        path = argv[1];
        offset = 0;
    } 
    else {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: read [-offset=N] <path>");
        return;
    }

    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }
    helper_response res;
    char *helper_argv[] = { path };
    int status = sendHelperRequestRW(helper_fd, READ, 1, helper_argv, offset, session, NULL, 0, &res);
    if(status != 0) {
        fprintf(stderr, "%s\n", res.msg);
        sendProtocolMsg(client_sfd, TEXT, -1, res.msg);
    }
    else if (res.payload_len > 0) {
        void* buf = malloc(res.payload_len);
        if (!buf) {
            sendProtocolMsg(client_sfd, TEXT, -1, "Server memory error");
        } else {
            if (readAll(helper_fd, buf, res.payload_len) == (ssize_t)res.payload_len) {
                msg_header client_hdr = {
                    .type = READCMD,
                    .status = 0,
                    .payloadLength = res.payload_len
                };
                writeAll(client_sfd, &client_hdr, sizeof(client_hdr));
                writeAll(client_sfd, buf, res.payload_len);
            }
            else {
                sendProtocolMsg(client_sfd, TEXT, -1, "Failed to read data from Helper");
            }
            free(buf);  
        }      
    }
    else {
        sendProtocolMsg(client_sfd, TEXT, 0, "File is empty");
    }
    
    close(helper_fd);

}
void handleWrite(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Log in first");
        return;
    }
    
    // strtok modifies the payload in-place, replacing spaces with '\0'
    // so argv pointers reference the original buffer with null-terminated tokens
    char *cmd_end = argv[argc - 1];  // last token
    cmd_end += strlen(argv[argc - 1]); // point past it
    cmd_end++;  // skip the null terminator that strtok inserted
    
    // file data begins after tokenized command
    void *file_buf = (void*)cmd_end;
    uint32_t data_len = hdr->payloadLength - (cmd_end - argv[0]);
    
    // Now parse the offset and path from argv as before
    int offset = 0;
    char *path = NULL;

    if (argc == 3) {
        const char *prefix = "-offset=";
        if (strncmp(argv[1], prefix, strlen(prefix)) == 0) {
            offset = atoi(argv[1] + strlen(prefix));
            path = argv[2];
        } else {
            sendProtocolMsg(client_sfd, TEXT, -1, "Usage: write [-offset=N] <path>");
            return;
        }
    } 
    else if (argc == 2) {
        path = argv[1];
        offset = 0;
    } 
    else {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: write [-offset=N] <path>");
        return;
    }

    printf("Writing %u bytes to %s at offset %d\n", data_len, path, offset);

    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal error: Helper unreachable");
        return;
    }

    helper_response res;
    char *helper_argv[] = { path };
    int status = sendHelperRequestRW(helper_fd, WRITE, 1, helper_argv, offset, session, file_buf, data_len, &res);

    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    close(helper_fd);
}

// we need to fork for background op and talk to the helper using the child
// if we do so no pollution on server_fds and no need to concurrent locks
void handleDownload(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
    int is_bg = 0;
    if (argc >= 4 && strcmp(argv[argc-1], "-b") == 0) {
        is_bg = 1;
    } else if (argc >= 4 && strcmp(argv[argc-1], "-b") != 0) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Usage: download <server_path> <client_path> [-b]", is_bg);
        return;
    }

    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Log in first", is_bg);
        return;
    }
    if (argc < 3 || argc > 4) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Usage: download <server_path> <client_path> [-b]", 0);
        return;
    }
    
    int data_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = 0 };
    if (bind(data_listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Internal: bind failed", is_bg);
        return;
    }
    listen(data_listener, 1);

    socklen_t len = sizeof(addr);
    getsockname(data_listener, (struct sockaddr*)&addr, &len);
    int data_port = ntohs(addr.sin_port);

    pid_t pid = fork();
    if (pid < 0) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Fork failed", is_bg);
        close(data_listener);
        return;
    }

    if (pid > 0) {
        char port_info[64];
        snprintf(port_info, sizeof(port_info), "DATA_PORT %d %s", data_port, argv[2]);
        sendProtocolMsgLocked(client_sfd, DOWNLOAD_RES, 0, port_info, 0);
        close(data_listener);
        return; 
    }
    release_socket_lock(client_sfd);
    int data_sfd = accept(data_listener, NULL, NULL);
    if (data_sfd < 0) _exit(1);
   
    close(data_listener);
    
    int helper_fd = connectToHelper();
    helper_response res;
    char *h_argv[] = { argv[1] }; 
    
    if (sendHelperRequestRW(helper_fd, DOWNLOAD, 1, h_argv, 0, session, NULL, 0, &res) == 0) {
        char buffer[16384];
        uint32_t total_to_read = res.payload_len;
        uint32_t total_received = 0;
        while (total_received < total_to_read) {
            uint32_t to_read = total_to_read - total_received;
            if (to_read > sizeof(buffer)) to_read = sizeof(buffer);

            ssize_t n = read(helper_fd, buffer, to_read);
            
            if (n <= 0) {
                fprintf(stderr, "[Debug] Connection closed or error before finishing file\n");
                break;
            }

            if (writeAll(data_sfd, buffer, n) < 0) {
                fprintf(stderr, "[Debug] Data socket write failed\n");
                break;
            }

            total_received += n;
        }
        close(data_sfd); // Tell client data port is finished
        
        char finished_msg[128];
        snprintf(finished_msg, sizeof(finished_msg), "download %s %s concluded", argv[1], argv[2]);
        sendProtocolMsgLocked(client_sfd, TEXT, 0, finished_msg, is_bg);
    } else {
        close(data_sfd);
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Download failed: %s", 
             (res.msg[0] != '\0') ? res.msg : "Helper error");
        sendProtocolMsgLocked(client_sfd, TEXT, -1, err_msg, is_bg);
    }

    close(helper_fd);
    _exit(0);
}

void handleUpload(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
    int is_bg = (argc >= 4 && strcmp(argv[argc-1], "-b") == 0);
    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Log in first", is_bg);
        return;
    }
    if (argc < 3 || argc > 4) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Usage: upload <client_path> <server_path> [-b]", 0);
        return;
    }

    int data_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = 0 };
    if (bind(data_listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Internal: bind failed", is_bg);
        return;
    }
    listen(data_listener, 1);

    socklen_t len = sizeof(addr);
    getsockname(data_listener, (struct sockaddr*)&addr, &len);
    int data_port = ntohs(addr.sin_port);
    pid_t pid = fork();
    if (pid < 0) {
        sendProtocolMsgLocked(client_sfd, TEXT, -1, "Fork failed", is_bg);
        close(data_listener);
        return;
    }
    if (pid > 0) {
        char port_info[64];
        snprintf(port_info, sizeof(port_info), "DATA_PORT %d %s", data_port, argv[1]);
        sendProtocolMsgLocked(client_sfd, UPLOAD_RES, 0, port_info, 0);
        close(data_listener);
        return;
    }   
    release_socket_lock(client_sfd);
    int data_sfd = accept(data_listener, NULL, NULL);
    close(data_listener); // close old fd since now accepted new con
    if (data_sfd < 0) _exit(1);

    int helper_fd = connectToHelper();
    helper_response res;
    char *h_argv[] = { argv[2] };
    if (sendHelperRequestRW(helper_fd, UPLOAD, 1, h_argv, 0, session, NULL, 0, &res) == 0) {
    
        char buffer[16384];
        ssize_t n;
        while ((n = read(data_sfd, buffer, sizeof(buffer))) > 0) {
            if (writeAll(helper_fd, buffer, n) < 0) {
                fprintf(stderr, "[Upload] Failed writing to helper\n");
                break;
            }
        }

        close(data_sfd);

        char finished_msg[128];
        snprintf(finished_msg, sizeof(finished_msg), "upload %s %s concluded", argv[2], argv[1]);
        sendProtocolMsgLocked(client_sfd, TEXT, 0, finished_msg, is_bg);   
    } else {
        close(data_sfd);
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Upload failed: %s", 
             (res.msg[0] != '\0') ? res.msg : "Helper error");
        sendProtocolMsgLocked(client_sfd, TEXT, -1, err_msg, is_bg);
    }
    close(helper_fd);
    exit(0);
}

void handleTransferRequest(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {

    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Login required");
        return;
    }
    if (argc < 3 || argc > 4) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: transfer_request <file> <dest_user>");
        return;
    }
    char* filename = argv[1];
    char* dest_user = argv[2];
    pid_t target_pid = 0;

    while (target_pid == 0) {
        sem_wait(&registry->mux);
        for (int i = 0; i < MAX_USERS; i++) {
            if (registry->online_users[i].is_active && strcmp(registry->online_users[i].username, dest_user) == 0) {
                target_pid = registry->online_users[i].handler_pid;
                break;
            }
        }
        sem_post(&registry->mux);
        // polling each sec releasing the lock
        if (target_pid == 0) {
            sleep(1);
        }
    }

    // asign new entry in the registry
    int slot_idx = -1;
    int assigned_id = -1;
    sem_wait(&registry->mux);
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (registry->pending[i].status == FREE) {
            slot_idx = i;
            break;
        }
    }

    if (slot_idx != -1) {
        assigned_id = ++(registry->global_id_counter); // incr glb id counter
        registry->pending[slot_idx].id = assigned_id;
        registry->pending[slot_idx].status = PENDING;
        snprintf(registry->pending[slot_idx].sender, MAX_USERNAME_LEN, "%s", session->username);
        snprintf(registry->pending[slot_idx].receiver, MAX_USERNAME_LEN, "%s", dest_user);
        snprintf(registry->pending[slot_idx].filename, FILENAME_MAX, "%s", filename);
    } 
    sem_post(&registry->mux);

    if (assigned_id == -1) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Server busy: Too many pending transfers");
        return;
    }

    kill(target_pid, SIGUSR1);

    char success[128];
    snprintf(success, sizeof(success), "Transfer request created with ID: %d. Waiting for recipient...", assigned_id);
    sendProtocolMsg(client_sfd, TEXT, 0, success);
}

void handleAcceptTransfer(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Login required");
        return;
    }
    if (argc != 3) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: accept <directory> <ID>");
        return;
    }
    char* target_dir = argv[1];
    int req_id = atoi(argv[2]);
    TransferRequest current_req;
    int found = 0;

    sem_wait(&registry->mux);
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (registry->pending[i].id == req_id && 
            registry->pending[i].status == NOTIFIED &&
            strcmp(registry->pending[i].receiver, session->username) == 0) {
            
            current_req = registry->pending[i];
            registry->pending[i].status = FREE;
            registry->pending[i].id = 0;
            found = 1;
            break;
        }
    }
    sem_post(&registry->mux);
    if (!found) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Error: ID not found or notification not yet processed.");
        return;
    }
    int helper_fd = connectToHelper();
    if (helper_fd < 0) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Internal Error: Helper unreachable");
        return;
    }
    char* helper_args[4];
    helper_args[0] = current_req.sender;
    helper_args[1] = current_req.filename;
    helper_args[2] = session->username;
    helper_args[3] = target_dir;

    helper_response res;
    int status = sendHelperRequest(helper_fd, TRANSFER, 4, helper_args, NULL, &res);
    
    sendProtocolMsg(client_sfd, TEXT, status, res.msg);
    close(helper_fd);
}
void handleRejectTransfer(int client_sfd, int argc, char* argv[], Server* server, ClientSession* session, msg_header* hdr) {
    if (session->state != STATE_LOGGED_IN) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Login required");
        return;
    }
    if (argc != 2) {
        sendProtocolMsg(client_sfd, TEXT, -1, "Usage: reject <request_id>");
        return;
    }
    int target_id = atoi(argv[1]);
    int found = 0;
    char sender_name[MAX_USERNAME_LEN] = {0};
    sem_wait(&registry->mux);
    for (int i = 0; i < MAX_TRANSFERS; i++) {

        if (registry->pending[i].id == target_id && 
            registry->pending[i].status != FREE) {
            if (strcmp(registry->pending[i].receiver, session->username) == 0) {
                strncpy(sender_name, registry->pending[i].sender, MAX_USERNAME_LEN - 1);

                
                registry->pending[i].status = REJECTED;
                registry->pending[i].id = 0;
                found = 1;
            } else {
                // ID exists, but it belongs to someone else
                found = -1; 
            }
            break;
        }
    }
    sem_post(&registry->mux);
    if (found == 1) {
        sendProtocolMsg(client_sfd, TEXT, 0, "Transfer rejected.");

        sem_wait(&registry->mux);
        for (int i = 0; i < MAX_USERS; i++) {
            if (registry->online_users[i].is_active && 
                strcmp(registry->online_users[i].username, sender_name) == 0) {
                
                kill(registry->online_users[i].handler_pid, SIGUSR1);
                break;
            }
        }
        sem_post(&registry->mux);

    } else if (found == -1) {
        sendProtocolMsg(client_sfd, TEXT, 1, "Error: This transfer is not for you.");
    } else {
        sendProtocolMsg(client_sfd, TEXT, 1, "Error: Request ID not found.");
    }
}