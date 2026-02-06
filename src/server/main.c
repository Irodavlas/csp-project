// takes arguments for server and starts it, listening on given ports
#include <stdio.h>
#include <stdlib.h>   // for atoi
#include <unistd.h> // fork

#include <sys/types.h>
#include <grp.h>

#include <fcntl.h>

#include "server/server.h"
#include "helper/helper.h"
#include "utils/utils.h"
#include "net/net.h"

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8080


#define PATH_MAX 512
#define SOCKT_MAX 128

SharedRegistry* registry = NULL;


int main(int argc, char* argv[]) {
    if (argc < 2 || argv[1][0] == '\0'){
        printf("Usage: %s <root_dir> [ip] [port]\n", argv[0]);
        return 1;
    } 
    char* root_dir = argv[1];
    char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    if (argc >= 3 && argv[2][0] != '\0') {
        ip = argv[2];           
    }
    if (argc >= 4 && argv[3][0] != '\0') {
        port = atoi(argv[3]);   
    }

    struct group *gr = getgrnam("myServerG");
    if (!gr) {
        fprintf(stderr, "Group myServerG not found\n");
        return 1;
    }
    gid_t sharedGroupId = gr->gr_gid;

    Server* server = createServer(root_dir, port, ip);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }

    // create tmp dir
    char socket_dir[PATH_MAX];
    snprintf(socket_dir, sizeof(socket_dir), "%s/tmp", server->Root);
    // owner: full access, groups: execute only to access socket file 
    if (createRootDirectory(socket_dir, 0770) < 0) {
        fprintf(stderr, "Failed to create tmp dir\n");
        return 1;
    }
    if (chown(socket_dir, -1, sharedGroupId) < 0) {
        fprintf(stderr, "Failed group change on tmp dir\n");
        return 1;
    }


    // creates unix socket for helper-server communication
    int listen_fd = createUnixSocket(server->Root, sharedGroupId);
    if (listen_fd < 0) { fprintf(stderr, "Failed to create helper socket\n"); return 1; }
    
    /*
    // create user file 
    char usersFilePath[PATH_MAX];
    snprintf(usersFilePath, sizeof(usersFilePath), "%s/users.txt", server->Root);
    int usersFd = open(usersFilePath, O_RDWR | O_CREAT, 0640); // create it if it doesnt exists 
    if (usersFd < 0) {
        perror("Failed to open users file");
        exit(1);
    }
    
    // fchown works on file descriptor, otw chown for filepath
    if (fchown(usersFd, -1, sharedGroupId) < 0) { // -1 if we dont want to change the id
        fprintf(stderr, "Failed group change on user.txt file\n");
        return 1;
    }
        */
    // create the helper

    SharedMemCleanup(); // for safety force clean up in case mem persisted
    initSharedRegistry();

    Helper* helper = CreateHelper(listen_fd, root_dir);   
    

    // fork the helper 
    pid_t helperPid = fork();
    if (helperPid < 0) {
        perror("fork helper");
        return 1;
    }
    else if (helperPid == 0) {
        if (setgid(sharedGroupId) != 0) {
            perror("setgid");
            _exit(1);
        }

        printf("Helper user id:%d, group:%d\n", geteuid(),getegid());
        runHelperLoop(helper);
        close(listen_fd);
        _exit(0);
    }
    close(listen_fd);
    
    

    setup_sigchld(); // sets up SIGCHLD handler 
    
    struct passwd* pw = userLookUp();
    if (!pw) {
        fprintf(stderr, "No non-root user available, cannot drop privileges!\n");
        return 1;
    }
    
    if (chroot(root_dir) < 0) {
        perror("chroot");
        return 1;
    }
    if (chdir("/") < 0) {
        perror("chdir failed");
        return 1;
    }
    printf("Server sandboxed to %s directory\n", root_dir);
    pw->pw_gid = sharedGroupId;
    if (dropPriviledges(pw) == -1) {
        return 1;
    }
    if (pw) printf("Running as user: %s\n", pw->pw_name);
    printf("permissions of user group:%d , user:%d \n", pw->pw_gid, pw->pw_uid);

    

    int status = startServer(server);
    if (status < 0) {
       
        return 1;
    }

    return 0;
}