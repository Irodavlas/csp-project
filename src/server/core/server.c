// defines the server process, setupds sockets, accept loop and client creation


#define _POSIX_C_SOURCE 200809L // needed for SA_RESTART macro and sigaction

#include "handler/handlers.h"
#include "core/server.h"
#include "helper/helper.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for malloc 
#include <unistd.h> // for chroot, chdir, setegid seteuid

#include <sys/stat.h> // for mkdir system call

#include <sys/socket.h>
#include <sys/wait.h> // waitfor 

#include <arpa/inet.h> // for inet_pton
#include <signal.h>

#include <sys/select.h>

#include <errno.h>

Server* createServer(char* root, int port, char* ip) {
    struct sockaddr_in addr; // used to bind socket to address using bind()
    Server* server = malloc(sizeof(Server)); 
    // we use -> cuz server is a pointer, equivalent to (*server).Port
    server -> Port = port;
    snprintf(server -> Root, sizeof(server -> Root), "%s", root);
    snprintf(server->Ip, sizeof(server->Ip), "%s", ip); // automatically puts \0, better than strncopy

    int s = socket(AF_INET, SOCK_STREAM, 0); // creates an endpoint for communicating and returns a file descriptor, -1 if error 
    if (s < 0) {
        perror("socket");
        return NULL;
    }
    server -> sfd = s;

    addr.sin_family = AF_INET;
    // network byte order = Big-endian (most significant byte first)
    // htons() translates an unsigned short integer into network byte order
    addr.sin_port = htons(server -> Port);

    int ret = inet_pton(AF_INET, server->Ip, &addr.sin_addr);
    if (ret <= 0) {
        if (ret == 0)
            fprintf(stderr, "Invalid IP format\n");
        else
            perror("inet_pton");
        close(server->sfd);
        return NULL;
    }
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero)); // zeroing the padding
    int opt = 1;
    if (setsockopt(server->sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { // avoids address already in use after stopping server
        perror("setsockopt");
        close(server->sfd);
        return NULL;
    }
    // cast to sockaddr from sockaddr_in
    if (bind(server -> sfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) { // assigns address to file descriptor 
        perror("bind");
        close(server->sfd);
        return NULL;
    }
    int created = createRootDirectory(server->Root, 0755); // rwx-rx-rx
    if (created < 0) {
        return NULL;
    }
    return server;
}  
int startServer(Server* server) {
    if (listen(server -> sfd, 20) == -1) {
        perror("listen");
        close(server->sfd);
        return -1;
    }
    sleep(1);
    printf("Server listening on %s:%d\n", server->Ip, server->Port);
    printf("Type 'exit' to shut down the server.\n");
    
    fd_set read_fds;
    int max_fds = server->sfd;

    while (1)
    {
        // macro to clear sete of fds at each iter 
        FD_ZERO(&read_fds);
        FD_SET(server->sfd, &read_fds); // socket
        FD_SET(STDIN_FILENO, &read_fds); // input 

        if (select(max_fds + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue; 
            perror("select");
            break;
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), stdin)) {
                buffer[strcspn(buffer, "\n")] = 0;
                if (strcmp(buffer, "exit") == 0) {
                    printf("Termination command received. Shutting down...\n");
                    break;
                }
            }
        }
        if (FD_ISSET(server->sfd, &read_fds)) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            int client_sfd = accept(server->sfd, (struct sockaddr*)&addr, &len); // storing client info for logging
            if (client_sfd < 0) {
                if (errno != EINTR) perror("accept");
                continue;
            } 
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                close(client_sfd);
                continue;
            }
            // child 
            if (pid == 0) {
                close(server->sfd); // close as child won't be accepting any request
                // call handle client 
                handleClient(client_sfd,server);
                close(client_sfd); 
                _exit(0); // performs no cleanup 
            } else { 
                // parent 

                close(client_sfd); // no need here
            }
        }
    }
    return 0;
}

void sigchld_handler(int signo) {
    pid_t pid;
    int status;
    char buf[100];
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int len = snprintf(buf, sizeof(buf), "Child %d exited with status %d\n", pid, status);
        write(STDOUT_FILENO, buf, len);
    }
}
void setup_sigchld(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask); // mask is the set of signals to be blocked during handler exec
    sa.sa_flags = SA_RESTART; // restart interrupted system calls.
    sa.sa_handler = sigchld_handler; // can be set to ignore, default or to a handler 

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    printf("Set up sigchld handler\n");
}

int createRootDirectory(const char* pathname, mode_t mode) {
    // create dir and set it as absolute path
    if (mkdir(pathname, mode) == -1) {
        if (errno == EEXIST) {
            struct stat st;
            if (stat(pathname, &st) == -1){
                perror("stat");
                return -1;
            }
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "%s exists but is not a directory\n", pathname);
                return -1;
            }
            printf("Directory already exists: %s\n", pathname);
            return 0;
        }
        perror("mkdir");
        return -1;
    }
    printf("Directory create with name:%s\n", pathname);
    // instead of chroot and chdir in here we do at user creation, server needs to be outside to create new users.
    return 0;
}
struct passwd* userLookUp(){
    const char* sudo_uid_str = getenv("SUDO_UID");
    struct passwd* pw = NULL;
    if (sudo_uid_str) {
        uid_t uid = (uid_t)atoi(sudo_uid_str);
        pw = getpwuid(uid); 
    }
    
    return pw;
}
int dropPriviledges(struct passwd* pw){
    if (getuid() == 0) {
        
        if (setgid(pw -> pw_gid) != 0) {
            perror("setgid");
            return -1;
        }
        
        if (setuid(pw -> pw_uid) != 0) {
            perror("setuid");
            return -1;
        }
    }
    return 0;
}

void performFullCleanup(Server* server, pid_t helperPid) {
    printf("\n[Cleanup] Starting graceful shutdown...\n");

    if (helperPid > 0) {
        printf("[Cleanup] Terminating Helper (PID: %d)...\n", helperPid);
        kill(helperPid, SIGTERM); 
    }

    if (registry != NULL) {
        sem_wait(&registry->mux);
        for (int i = 0; i < MAX_USERS; i++) {
            if (registry->online_users[i].is_active) {
                pid_t pid = registry->online_users[i].handler_pid;
                printf("[Cleanup] Killing handler for %s (PID: %d)\n", 
                        registry->online_users[i].username, pid);
                
                kill(pid, SIGTERM);
                waitpid(pid, NULL, 0);
            }
        }
        sem_post(&registry->mux);
    }

    printf("[Cleanup] Removing Shared Memory and Semaphores...\n");
    SharedMemCleanup(); 

    if (server) {
        close(server->sfd);
        free(server);
    }

    printf("[Cleanup] Done. Safe to restart.\n");
}