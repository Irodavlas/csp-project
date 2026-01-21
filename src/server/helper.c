// here we have the minimal priviledge helper to create users on demand and users' directories, communicates via socket with other process

#include "helper.h"
#include "utils.h"


#include <stdlib.h>

#include <pwd.h> //getpwnam
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#define PATH_MAX 256


typedef void (*cmd_func) (int server_fds, int argc, char* argv[]);


Helper* CreateHelper(int socket_fd, char* rootDir) {
    Helper* helper = malloc(sizeof(Helper));
    helper->socket_fds = socket_fd;
    
    strncpy(helper->rootDir, rootDir, sizeof(helper->rootDir)-1);
    helper->rootDir[sizeof(helper->rootDir)-1] = '\0';


    
    return helper;
}

void runHelperLoop(Helper* helper) {
    printf("helper set-up and ready\n");
    while (1) {
        // should fork for each arriving command for parallelism
        struct sockaddr_un addr;
        socklen_t len = sizeof(addr);
        int server_fds = accept(helper->socket_fds,(struct sockaddr*)&addr, &len);
        // accept incoming connections 
        if (server_fds < 0) { perror("accept"); return;}

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(server_fds);
            continue;
        }
        if (pid == 0) {
            close(helper->socket_fds);
            handleCommands(helper, server_fds);
            close(server_fds);
            _exit(0);
        }
        else {
            close(server_fds);
        }
    }
}
/*
IMPORTANTE FOR NEXT TIME
since we fork per commands received, we could as the helper in the function handling the command:
    - chdir to user home (if use chroot ls wont work)
    - drop priv to user
    - execute action
    - return response to server
    - kill child

*/
void handleCommands(Helper* helper, int server_fds){
    // if else if chain for priviledged commands
    while (1) {
        helper_request req;
        
        helper_response res = {0};
        res.status = -1;

        if (read(server_fds, &req, sizeof(req)) <= 0) {
            perror("[Helper] read");
            break;
        }
        switch(req.cmd) {
            case CREATE_USER:
                char* args[MAXARGS];
                char* p = req.payload;
                for (int i = 0; i < req.argc && i < MAXARGS; i++) {
                    args[i] = p;
                    p += strlen(p) + 1;
                }
                mode_t mode = strtol(args[1], NULL, 8);
                int stat = CreateSystemUser(helper->rootDir, args[0], mode, res.msg, sizeof(res.msg));
                res.status = stat;
                if (stat == 0 && strlen(res.msg) == 0) {
                    strncpy(res.msg, "> User created successfully\n", sizeof(res.msg) -1);
                    res.msg[sizeof(res.msg)-1] = '\0';
                }
                break;
            case LOGIN:
                char *username = req.payload;
                struct passwd *pw = getpwnam(username);

                if (!pw) {
                    res.status = -1;
                    strncpy(res.msg, "> Username not found\n", sizeof(res.msg) - 1);
                    break;
                }

                res.status = 0;
                res.uid = pw->pw_uid;
                res.gid = pw->pw_gid;

                strncpy(res.home, pw->pw_dir, sizeof(res.home) - 1);
                res.home[sizeof(res.home) - 1] = '\0';

                strncpy(res.msg, "> Login successful\n", sizeof(res.msg) - 1);
                break;
            default:
                strncpy(res.msg, "Command not available on the helper", sizeof(res.msg)-1);
                res.msg[sizeof(res.msg)-1] = '\0';
                
        }
        if (writeByteStream(server_fds, (char*)&res, sizeof(res)) != sizeof(res)) {
            perror("Failed to send helper response");
        }
    }
}
int dropPriviledgesTemp(struct passwd* pw) {

    // set process real and effective IDs to unpriviledges user 
    if (getuid() == 0) {
        if (setegid(pw -> pw_gid) != 0) {
            perror("setegid");
            return 1;
        }
        if (seteuid(pw->pw_uid) != 0) {
            perror("seteuid");
            return -1;
        }
    }
    printf("Privileges temporarily dropped to user: %s\n", pw->pw_name);
    return 0;
}
int regainRoot() {
    if (seteuid(0) != 0 || setegid(0) != 0) {
        perror("Failed to regain root");
        return -1;
    }
    printf("Privileges regained as root\n");
    return 0;
}


// utils

int userExists(const char *username) {
    struct passwd *pw = getpwnam(username);
    return pw != NULL;
}



int CreateSystemUser(
    const char* rootDir,     
    const char* username,
    mode_t privileges,
    char msg[],
    size_t msgLen
) {
    char absRoot[PATH_MAX];
    char homeDir[ABS_PATH];

    // root dir to absolute path
    if (!realpath(rootDir, absRoot)) {
        perror("realpath");
        snprintf(msg, msgLen, "> invalid root directory\n");
        return -1;
    }

    snprintf(homeDir, sizeof(homeDir), "%s/%s", absRoot, username);

    if (getpwnam(username) != NULL) {
        snprintf(msg, msgLen, "> error creating user, username taken");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        snprintf(msg, msgLen, "> internal server error");
        return -1;
    }

    if (pid == 0) {
        execlp(
            "adduser",
            "adduser",
            "--disabled-password",
            "--gecos", "",
            "--ingroup", "userGroup",
            "--home", homeDir,
            "--no-create-home",
            username,
            (char *)NULL
        );
        perror("execlp(adduser)");
        _exit(1);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0 ||
        !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {

        snprintf(msg, msgLen, "> error creating user\n");
        return -1;
    }

    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        snprintf(msg, msgLen, "> internal server error");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        return -1;
    }

    
    if (mkdir(homeDir, privileges) != 0) {
        perror("mkdir");
        snprintf(msg, msgLen, "> error creating home directory\n");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        return -1;
    }

   
    if (chown(homeDir, pwd->pw_uid, pwd->pw_gid) != 0) {
        perror("chown");
        snprintf(msg, msgLen, "> error setting home ownership\n");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        return -1;
    }
    return 0;
}

void CheckUserLoggigIn(const char* username, char msg[], size_t msgLen){

    return;
}