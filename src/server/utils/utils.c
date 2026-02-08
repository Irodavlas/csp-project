// moslty for error wrapping and logging


// TCP streams read and writes must be handled with care
// https://incoherency.co.uk/blog/stories/reading-tcp-sockets.html

#include "net/net.h"
#include "utils/utils.h"    

#include <grp.h>            // Required for initgroups()
#include <fcntl.h>          // Required for open(), fcntl(), and F_WRLCK
#include <stdlib.h>         // Required for strtol() 
#include <limits.h>         // Required for PATH_MAX 
#include <errno.h>
#include <sys/stat.h>
#include <string.h>  // For strlen
#include <ctype.h>   // For isalpha
#include <unistd.h>  // For chown, chroot, chdir, getuid, seteuid, close


bool isUsernameValid(char* username) {
    if (!username) {
        return false;
    }
    size_t len = strlen(username);
    if (len == 0 || len > MAX_USERNAME_LEN) {
        return false;
    }
    for (char* p = username; *p; p++) {
        if (!isalpha((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}


int createUserDirectory(const char* pathname, uid_t uid, gid_t gid, mode_t mode) {
    printf("mode = %04o\n", mode);
    
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
    } else {
        printf("Directory create with name:%s\n", pathname);
    }
    if (chown(pathname, uid, gid) != 0) {
        perror("chown failed");
        return -1;
    }

    if (chmod(pathname, mode) != 0) {
        perror("chmod failed");
        return -1;
    }
    return 0;
}

int dropPrivilegesTemp(const ClientSession *cs) {
    if (getuid() != 0) {
        fprintf(stderr, "Not running as root, cannot drop privileges\n");
        return -1;
    }
    //   The  initgroups() function initializes the group access list by reading
    //   the group database /etc/group and using all groups of which user  is  a
    //   member
    // to avoid having other groups bypassing our permission checks
    if (initgroups(cs->username, cs->gid) != 0) {
        perror("initgroups");
        return -1;
    }

    if (setegid(cs->gid) != 0) {
        perror("setegid");
        return -1;
    }

    if (seteuid(cs->uid) != 0) {
        perror("seteuid");
        return -1;
    }

    printf("Privileges temporarily dropped to user: %s\n", cs->username);
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

int sandboxUserToHisHome(const ClientSession* session){
    printf("Session home:%s\n", session->home);
    if (chroot(session->home) == -1) {
        perror("chroot");
        return -1;
    }
    if (chdir(session->workdir) == -1) {
        perror("chdir");
        return -1;
    }
    if (dropPrivilegesTemp(session) == -1) {
        return -1;
    }
    return 0;
}
int sandboxUserToRoot(const ClientSession* session, char* rootdir){
    if (chroot(rootdir) == -1) {
        perror("chroot");
        return -1;
    }
    char realPath[2056];
    printf("workdir:%s\n", session->workdir);
   
    snprintf(realPath, sizeof(realPath), "/%s%s", session->username, session->workdir);
    printf("real path:%s\n", realPath);
    
    if (chdir(realPath) == -1) {
        perror("chdir");
        return -1;
    }
    if (dropPrivilegesTemp(session) == -1) {
        return -1;
    }
    return 0;
}


int acquireUserCreationLock(const char* lock_file_path) {
    int fd = open(lock_file_path, O_RDWR);
    if (fd < 0) {
        perror("open lock file");
        return -1;
    }
    
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl lock");
        close(fd);
        return -1;
    }
    
    return fd;
}

void releaseUserCreationLock(int fd) {
    if (fd < 0) return;
    
    struct flock fl;
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    fcntl(fd, F_SETLK, &fl);
    close(fd);
}
