// here we have the minimal priviledge helper to create users on demand and users' directories, communicates via socket with other process

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>    
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>     
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/mman.h>

#include "helper/helper.h"
#include "net/net.h"
#include "utils/utils.h"

#define USER_CREATION_LOCK_FILENAME ".user_creation.lock"
static char lock_file_path[PATH_MAX];


typedef void (*cmd_func) (int server_fds, int argc, char* argv[]);


void initSharedRegistry() {
    int fd = shm_open("/server_registry", O_CREAT | O_RDWR, 0660);
    if (fd == -1) {
        perror("shm_open");
        exit(1);
    }
    // shared memory is an obj of 0 bytes, with ftruncate we define the size
    if (ftruncate(fd, sizeof(SharedRegistry)) == -1) {
        perror("truncate");
        exit(1);
    }
    registry = mmap(NULL, sizeof(SharedRegistry), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (registry == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    if (sem_init(&registry->mux, 1, 1) == -1) {
        perror("sem_init");
        exit(1);
    }
    printf("Shared memory segment initialized\n");
    memset(registry->online_users, 0, sizeof(registry->online_users));
    memset(registry->pending, 0, sizeof(registry->pending));
}

void SharedMemCleanup() {
   if (registry != NULL) {
        sem_destroy(&registry->mux);
    }
    if (shm_unlink("/server_registry") == -1) {
        if (errno != ENOENT) {
            perror("[Cleanup] shm_unlink failed");
        }
    } else {
        printf("[Cleanup] Shared memory /server_registry unlinked.\n");
    }
    
    if (lock_file_path[0] != '\0') {
        unlink(lock_file_path);
        printf("[Cleanup] User creation lock file removed.\n");
    }
    unlink("/tmp/helper.sock");
    
}

Helper* CreateHelper(int socket_fd, char* rootDir) {
    Helper* helper = malloc(sizeof(Helper));
    helper->socket_fds = socket_fd;
    char absRoot[PATH_MAX];
    if (!realpath(rootDir, absRoot)) {
        perror("Invalid rootDir path");
        return NULL;
    }
    strncpy(helper->rootDir, absRoot, sizeof(helper->rootDir)-1);
    helper->rootDir[sizeof(helper->rootDir)-1] = '\0';
    return helper;
}

void runHelperLoop(Helper* helper) {
    snprintf(lock_file_path, sizeof(lock_file_path), 
             "%s/%s", helper->rootDir, USER_CREATION_LOCK_FILENAME);
    
    int test_fd = open(lock_file_path, O_CREAT | O_WRONLY, 0600);
    if (test_fd < 0) {
        perror("Failed to create lock file");
        return;
    }
    close(test_fd);

    printf("helper set-up and ready\n");
    printf("[Helper] Lock file created at %s\n", lock_file_path);
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

void handleCommands(Helper* helper, int server_fds){
    // if else if chain for priviledged commands
    while (1) {
        helper_request_header hdr;
        ssize_t n = readAll(server_fds, &hdr, sizeof(hdr));
        if (n <= 0) {
            if (n == 0) printf("[Helper] Server closed connection\n");
            else perror("[Helper] read error");
            break;
        }
        printf("[Helper] hdr.cmd=%d argc=%u payload_len=%u data_len=%u offset=%d\n",
        hdr.cmd, hdr.argc, hdr.payload_len, hdr.data_len, hdr.offset);

        char* payload = NULL;
        char* args[MAXARGS] = {NULL};

        if (hdr.payload_len > 0) {
            payload = malloc(hdr.payload_len);
            if (readAll(server_fds, payload, hdr.payload_len) <= 0) {
                free(payload);
                break;
            }
            // args array unpacking
            char* p = payload;
            for (int i = 0; i < hdr.argc && i < MAXARGS; i++) {
                args[i] = p;
                p += strlen(p) + 1; 
            }
        }
        void *data_buf = NULL;

        if (hdr.data_len > 0) {
            data_buf = malloc(hdr.data_len);
            if (!data_buf || readAll(server_fds, data_buf, hdr.data_len) <= 0) {
                free(payload);
                free(data_buf);
                break;
            }
        }
        helper_response res;
        memset(&res, 0, sizeof(res));
        res.cmd = hdr.cmd;
        res.status = -1;
        switch(hdr.cmd) {
            case CREATE_USER: {
                mode_t mode = strtol(args[1], NULL, 8);
                res.status = CreateSystemUser(helper->rootDir, args[0], mode, res.msg, sizeof(res.msg));
                writeAll(server_fds, &res, sizeof(res));
                break;
            }

            case LOGIN:
                handleHelperLogin(server_fds, args[0], &res);
                break;

            case LS:
                handleHelperLs(server_fds, &hdr, args[0], helper->rootDir, &res);
                break;
            case CD:
                ChangeDirectory(server_fds, &hdr, args[0], &res);
                break;
            case CREATE_FILE: {
                mode_t mode = strtol(args[1], NULL, 8);
                int makeDir = 0;

                if (hdr.argc > 2 && strcmp(args[2], "-d") == 0) {
                    makeDir = 1;
                }
                HandlerHelperCreateFile(server_fds, &hdr, args[0], mode, makeDir, &res);
                break;
            }
            case CHMOD: {
                mode_t mode = strtol(args[1], NULL, 8);
                HandleHelperChmod(server_fds, &hdr, args[0], mode, &res);
                break;
            }
            case DELETE:
                HandleHelperDelete(server_fds, &hdr, args[0], &res);
                break;
            case MOVE:
                HandleHelperMove(server_fds, &hdr, args[0], args[1], &res);
                break;
            case READ:
                HandleHelperRead(server_fds, &hdr, args[0], hdr.offset, &res);
                break;
            case WRITE:
                HandleHelperWrite(server_fds, &hdr, args[0], hdr.offset,data_buf, hdr.data_len, &res);
                break;
            case DOWNLOAD:
                HandleHelperDownload(server_fds, &hdr, args[0], &res);
                break;
            case UPLOAD:
                HandleHelperUpload(server_fds, &hdr, args[0], &res);
                break;
            case TRANSFER:
                HandleHelperTransfer(server_fds, &hdr, helper->rootDir, args[0], args[1], args[2], args[3], &res);
                break;
            default:
                strncpy(res.msg, "Command not available on the helper", sizeof(res.msg)-1);
                writeAll(server_fds, &res, sizeof(res));
        }
        if (payload) {
            free(payload);
            payload = NULL; 
        }
        if (data_buf) {
            free(data_buf);
            data_buf = NULL; 
        }
    }
}



int CreateSystemUser(
    const char* rootDir,     
    const char* username,
    mode_t privileges,
    char msg[],
    size_t msgLen
) {
    
    char homeDir[ABS_PATH];
    snprintf(homeDir, sizeof(homeDir), "%s/%s", rootDir, username);

    int lock_fd = acquireUserCreationLock(lock_file_path);
    if (lock_fd < 0) {
        snprintf(msg, msgLen, "internal server error");
        return -1;
    }

    if (getpwnam(username) != NULL) {
        snprintf(msg, msgLen, "error creating user, username taken");
        releaseUserCreationLock(lock_fd);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        snprintf(msg, msgLen, "internal server error");
        releaseUserCreationLock(lock_fd);
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

        snprintf(msg, msgLen, "error creating user");
        releaseUserCreationLock(lock_fd);
        return -1;
    }

    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        snprintf(msg, msgLen, "internal server error");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        releaseUserCreationLock(lock_fd);
        return -1;
    }

    
    if (mkdir(homeDir, privileges) != 0) {
        perror("mkdir");
        snprintf(msg, msgLen, "error creating home directory");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        releaseUserCreationLock(lock_fd);
        return -1;
    }

   
    if (chown(homeDir, pwd->pw_uid, pwd->pw_gid) != 0) {
        perror("chown");
        snprintf(msg, msgLen, "error setting home ownership");

        if (fork() == 0) {
            execlp("deluser", "deluser", "--remove-home", username, NULL);
            _exit(1);
        }
        wait(NULL);
        releaseUserCreationLock(lock_fd);
        return -1;
    }
    snprintf(msg, msgLen, "User created succesfully");
    releaseUserCreationLock(lock_fd);
    return 0;
}

void handleHelperLs(int server_fd, helper_request_header *hdr, char* path, const char *rootDir, helper_response *res) {
    FileEntry entries[1024]; 
    size_t count = 0;
    DIR *dir = NULL;

   if (sandboxUserToRoot(&hdr->session, (char*)rootDir) == -1) {
        strncpy(res->msg, "Internal server error", sizeof(res->msg) - 1);
        writeAll(server_fd, res, sizeof(helper_response));
        _exit(1);
    }

    // returns pointer to dir, to its first entry
    dir = opendir(path); 
    if (!dir) {
        res-> status = -1;
        snprintf(res->msg, sizeof(res->msg),
                 "ls failed: %s", strerror(errno));
        goto out;
    }
    struct dirent* entry;
    
    //read first and next entry in dir 
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
      
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0) {
            perror("stat");
            continue;
        }
        
        FileEntry *e = &entries[count];
        memset(e, 0, sizeof(FileEntry));

        strncpy(e->name, entry->d_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name)-1] = '\0';
        
        e->perms[0] = S_ISDIR(st.st_mode)  ? 'd' :
                   S_ISLNK(st.st_mode)  ? 'l' :
                   '-' ;
        e->perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
        e->perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
        e->perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
        e->perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
        e->perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
        e->perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
        e->perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
        e->perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
        e->perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
        e->perms[10] = '\0';

        e->size = st.st_size;
        count++;
    }

    res->status = 0;
    strncpy(res->msg, "Success", sizeof(res->msg) - 1);
out:
    if (dir != NULL)
        closedir(dir);          

    if (regainRoot() == -1) {
        _exit(1); 
    }
    res->data.ls.count = (uint32_t)count;
    res->payload_len = count * sizeof(FileEntry);
    writeAll(server_fd, res, sizeof(helper_response));
    if (res->status == 0 && res->payload_len > 0) {
        if (writeAll(server_fd, entries, res->payload_len) < 0) {
            perror("[Helper] Failed to send LS payload");
        }
    }

}

void ChangeDirectory(int server_fd, helper_request_header *hdr, char* path, helper_response *res) {
    res->status = -1;

    if (sandboxUserToHisHome(&hdr->session) == -1) {
        strncpy(res->msg, "Internal server error", sizeof(res->msg) - 1);
        writeAll(server_fd, res, sizeof(helper_response));
        _exit(1);
    }
    char newcwd[ABS_PATH];

    if (chdir(path) == -1) {
        snprintf(res->msg, sizeof(res->msg),
                 "cd failed: %s", strerror(errno));
        goto out;

    }
    if (getcwd(newcwd, sizeof(newcwd)) == NULL) {
        strncpy(res->msg, "Failed to resolve directory",
                sizeof(res->msg) - 1);
        goto out;
    }
    res->status = 0;
    snprintf(res->msg, sizeof(res->msg), "Directory changed");
    snprintf(res->data.cd.cwd, sizeof(res->data.cd.cwd), "%s", newcwd);

out:
    if (regainRoot() == -1) {
        _exit(1);   // helper without root is best to close it
    }
    res->payload_len = strlen(res->data.cd.cwd) + 1;
    writeAll(server_fd, res, sizeof(helper_response));
}

void handleHelperLogin(int server_fd, char* username, helper_response* res) {
    if (!username) {
        res->status = -1;
        strncpy(res->msg, "Missing username", sizeof(res->msg)-1);
    } else {
        struct passwd *pw = getpwnam(username);
        if (pw == NULL) {
            res->status = -1;
            snprintf(res->msg, sizeof(res->msg), "User %s not found", username);
        } else {
            res->status = 0;
            res->data.login.uid = pw->pw_uid;
            res->data.login.gid = pw->pw_gid;
            strncpy(res->data.login.home, pw->pw_dir, sizeof(res->data.login.home));
            //strncpy(res->msg, "Login data found", sizeof(res->msg)-1);
        }
    }
    writeAll(server_fd, res, sizeof(helper_response));
}

void HandlerHelperCreateFile(int server_fd, helper_request_header *hdr, const char* filename, mode_t privileges,int makeDir, helper_response *res) {
    int fd = -1;
    int lockFd = -1;
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        strncpy(res->msg, "Sandbox error", sizeof(res->msg) - 1);
        writeAll(server_fd, res, sizeof(helper_response));
        _exit(1);
    }
    if (makeDir) {
        if (mkdir(filename, privileges) != 0) {
            snprintf(res->msg, sizeof(res->msg), "mkdir failed: %s", strerror(errno));
            res->status = -1;
        } else {
            snprintf(res->msg, sizeof(res->msg), "Directory created successfully");
            res->status = 0;
        }
        writeAll(server_fd, res, sizeof(*res));
        if (regainRoot() == -1) _exit(1);
        return;
    }
    int dirfd = open(".", O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) {
        strncpy(res->msg, "Dir open error", sizeof(res->msg)-1);
        goto out;
    }
    fd = openat(dirfd, filename,
                O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW,
                privileges);

    close(dirfd);
    
    if (fd < 0) {
        perror("openat");
        strncpy(res->msg, "Create failed", sizeof(res->msg)-1);
        goto out;
    }
    lockFd = lock_file(filename, LOCK_EXCLUSIVE);
    if (lockFd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Cannot lock file for creation");
        close(dirfd);
        goto out;
    }
    
    res->status = 0;
    strncpy(res->msg, "File created succesfully", sizeof(res->msg) - 1);
    
out:
    if (fd >= 0) {
        close(fd);
    }
    if (lockFd >= 0) {
        unlock_file(lockFd);
    }
    if (regainRoot() == -1) {
        _exit(1);   
    }
    writeAll(server_fd, res, sizeof(helper_response));
}

void HandleHelperChmod(int server_fd, helper_request_header *hdr, const char* filename, mode_t privileges, helper_response *res) {
    int lockFd = -1;
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        strncpy(res->msg, "Sandbox error", sizeof(res->msg) - 1);
        writeAll(server_fd, res, sizeof(helper_response));
        _exit(1);
    }
    lockFd = lock_file(filename, LOCK_EXCLUSIVE);
    if (lockFd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Cannot lock file for chmod");
        goto out;
    }
    if (chmod(filename, privileges) != 0) {
        snprintf(res->msg, sizeof(res->msg), "chmod failed: %s", strerror(errno));
        goto out;
    }
    res->status = 0;
    snprintf(res->msg, sizeof(res->msg), "Permissions updated successfully");

out:
    if (lockFd >= 0) unlock_file(lockFd);
    if (regainRoot() == -1) {
        _exit(1);   
    }
    writeAll(server_fd, res, sizeof(helper_response));

}

void HandleHelperDelete(int server_fd, helper_request_header *hdr, const char* path, helper_response *res) {
    int lockFd = -1;
    struct stat st;

    if (sandboxUserToHisHome(&hdr->session) == -1) {
        strncpy(res->msg, "Sandbox error", sizeof(res->msg) - 1);
        writeAll(server_fd, res, sizeof(helper_response));
        _exit(1);
    }
    if (stat(path, &st) != 0) {
        snprintf(res->msg, sizeof(res->msg), "Delete failed: %s", strerror(errno));
        goto out;
    }

    int ret = -1;
    if (S_ISDIR(st.st_mode)) {
        // only works if the directory is EMPTY
        ret = rmdir(path); 
        if (ret != 0) {
            snprintf(res->msg, sizeof(res->msg), "Delete directory failed: %s", strerror(errno));
            goto out;
        }
    } else {
        lockFd = lock_file(path, LOCK_EXCLUSIVE);
        if (lockFd < 0) {
            snprintf(res->msg, sizeof(res->msg), "Cannot lock file for delete");
            goto out;
        }

        ret = unlink(path);
        if (ret != 0) {
            snprintf(res->msg, sizeof(res->msg), "Delete file failed: %s", strerror(errno));
            goto out;
        }
        
        unlock_file(lockFd);
        lockFd = -1; 
    }

    res->status = 0;
    snprintf(res->msg, sizeof(res->msg), "Deleted successfully");

out: 
    if (lockFd >= 0) {
        unlock_file(lockFd);
    }
    if (regainRoot() == -1) {
        _exit(1);   
    }
    writeAll(server_fd, res, sizeof(helper_response));
}
void HandleHelperMove(int server_fd, helper_request_header *hdr, const char* path1, const char* path2, helper_response *res) {
    int lockFd = -1;
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        snprintf(res->msg, sizeof(res->msg), "Sandbox error");
        writeAll(server_fd, res, sizeof(*res));
        _exit(1);
    }
    lockFd = lock_file(path1, LOCK_EXCLUSIVE);
    if (lockFd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Cannot lock source file for move");
        goto out;
    }
    struct stat stSrc, stDest;
    if (stat(path1, &stSrc) != 0) {
        snprintf(res->msg, sizeof(res->msg), "Move failed: source does not exist: %s", strerror(errno));
        goto out;
    }
    if (stat(path2, &stDest) != 0 || !S_ISDIR(stDest.st_mode)) {
        snprintf(res->msg, sizeof(res->msg), "Move failed: destination is not a directory or doesn't exists");
        goto out;
    }

    // build path to dst directory using name of src file
    char *baseName = strrchr(path1, '/');
    if (baseName != NULL) {
        baseName = baseName + 1;  
    } else {
        baseName = (char *)path1;  
    }

    char dstPath[PATH_MAX];
    snprintf(dstPath, sizeof(dstPath), "%s/%s", path2, baseName);
    
    struct stat stCheck;
    if (stat(dstPath, &stCheck) == 0) {
        snprintf(res->msg, sizeof(res->msg), "Move failed: destination file already exists");
        goto out;
    }
    if (rename(path1, dstPath) != 0) {
        snprintf(res->msg, sizeof(res->msg), "Move failed: %s", strerror(errno));
        goto out;
    }

    res->status = 0;
    snprintf(res->msg, sizeof(res->msg), "Moved successfully");

out:
    if (lockFd >= 0) unlock_file(lockFd);
    if (regainRoot() == -1) _exit(1);
    writeAll(server_fd, res, sizeof(*res));
}

void HandleHelperRead(int server_fd, helper_request_header *hdr, const char* path, int offset, helper_response *res) {
    int lockFd = -1;
    struct stat st;

    printf("path:%s & offset:%d\n", path, offset);
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        snprintf(res->msg, sizeof(res->msg), "Sandbox error");
        writeAll(server_fd, res, sizeof(*res));
        _exit(1);
    }
   
    lockFd = lock_file(path, LOCK_SHARED);   
    if (lockFd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Lock failed: %s", strerror(errno));
        goto out;
    }
    // check if it's a regular file and that offset is not going past EOF
    if (fstat(lockFd, &st) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Stat failed: %s", strerror(errno));
        goto out;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(res->msg, sizeof(res->msg), "Not a regular file");
        goto out;
    }
    if ((off_t)offset > st.st_size) {
        snprintf(res->msg, sizeof(res->msg), "Offset beyond EOF");
        goto out;
    }
    // on error the value (off_t)-1 is returned otw returns the set offset
    if (lseek(lockFd, offset, SEEK_SET) == (off_t)-1) {
        snprintf(res->msg, sizeof(res->msg), "Seek failed");
        goto out;
    }

    char buf[4096];
    ssize_t n;

    n = read(lockFd, buf, sizeof(buf));
    if (n < 0) {
        fprintf(stderr, "[HelperRead] read error: %s\n", strerror(errno));
        snprintf(res->msg, sizeof(res->msg), "Internal error reading file");
        goto out;
    }

    res->status = 0;
    res->payload_len = (uint32_t)n; 
    snprintf(res->msg, sizeof(res->msg), "Success");

out:
    
    if (lockFd >= 0) unlock_file(lockFd);

    if (regainRoot() == -1)
        _exit(1);
    
        // header
    if (writeAll(server_fd, res, sizeof(helper_response)) < 0) {
        perror("[Helper] Failed to send response header");
    }
    if (res->status == 0 && res->payload_len > 0) { 
        if (writeAll(server_fd, buf, res->payload_len) < 0) {
            perror("[Helper] Failed to send file payload");
        }
    }
}
    
void HandleHelperWrite(int server_fd, helper_request_header *hdr, const char* path, int offset, void *data,
                       uint32_t data_len, helper_response *res) {                        

    struct stat st;

    printf("path:%s, offset:%d\n", path, offset);
    
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        snprintf(res->msg, sizeof(res->msg), "Sandbox error");
        writeAll(server_fd, res, sizeof(*res));
        _exit(1);
    }
   
    int fd = open(path, O_RDWR | O_CREAT, 0700);
    if (fd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Open failed: %s", strerror(errno));
        goto out;
    }
    if (lock_fd(fd, LOCK_EXCLUSIVE) < 0) {
        close(fd);
        snprintf(res->msg, sizeof(res->msg), "Error locking fd: %s", strerror(errno));
        goto out; 
    }


    if (fstat(fd, &st) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Stat failed: %s", strerror(errno));
        goto out;
    }

    if (!S_ISREG(st.st_mode)) {
        snprintf(res->msg, sizeof(res->msg), "Not a regular file");
        goto out;
    }
    // if beyond the EOF we manually pad to avoid null holes 
    //created by lskeeing after EOF
    if (offset > st.st_size) { 
        if (lseek(fd, 0, SEEK_END) == (off_t)-1) goto out;
        char space = ' ';
        for (int i = st.st_size; i < offset; i++) {
            if (write(fd, &space, 1) < 0) {
                snprintf(res->msg, sizeof(res->msg), "Padding failed");
                goto out;
            }
        }

    } else {
        if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
            snprintf(res->msg, sizeof(res->msg), "Seek failed: %s", strerror(errno));
            goto out;
        }
    }

    

    ssize_t n = write(fd, data, data_len);
    if (n < 0) {
        snprintf(res->msg, sizeof(res->msg), "Write failed: %s", strerror(errno));
        goto out;
    }

    if ((uint32_t)n != data_len) {
        snprintf(res->msg, sizeof(res->msg), "Partial write: wrote %ld of %u bytes", n, data_len);
        goto out;
    }

    res->status = 0;
    res->payload_len = 0;
    snprintf(res->msg, sizeof(res->msg), "Success");

out:
    if (fd >= 0) {
        close(fd); // should also release the lock associated with it 
    }
    

    if (regainRoot() == -1)
        _exit(1);
    
    if (writeAll(server_fd, res, sizeof(helper_response)) < 0) {
        perror("[Helper] Failed to send response header");
    }

}


void HandleHelperDownload(int server_fd, helper_request_header *hdr, const char* path, helper_response *res) {
    fprintf(stderr, "[Helper] Starting download for path: %s\n", path);
    int fd = -1;
    struct stat st;
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        snprintf(res->msg, sizeof(res->msg), "Sandbox error");
        writeAll(server_fd, res, sizeof(*res));
        return;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Open failed: %s", strerror(errno));
        writeAll(server_fd, res, sizeof(helper_response));
        return;
    }
    if (lock_fd(fd, LOCK_SHARED) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Lock failed");
        goto out;
    }
    
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        snprintf(res->msg, sizeof(res->msg), "Invalid file type");
        goto out;
    }
    res->status = 0;
    res->payload_len = (uint32_t)st.st_size;
    snprintf(res->msg, sizeof(res->msg), "Success");

    /*
    fprintf(stderr, "[Helper] Waiting 10 seconds before sending response (for testing)...\n");
    sleep(5);
    fprintf(stderr, "[Helper] Delay complete, sending response now\n");
    */
    if (writeAll(server_fd, res, sizeof(helper_response)) < 0) {
        goto out;
    }
    fprintf(stderr, "[Helper] Beginning stream to server_fd...\n");
    char stream_buf[16384];
    ssize_t n;
    while ((n = read(fd, stream_buf, sizeof(stream_buf))) > 0) {
        if (writeAll(server_fd, stream_buf, n) < 0) break;
    }
out: 
    if (res->status != 0) {
        writeAll(server_fd, res, sizeof(helper_response));
    }
    if (fd >= 0) {
        unlock_fd(fd);
        close(fd);
    }
    if (regainRoot() == -1) {
        _exit(1);
    }
}

void HandleHelperUpload(int server_fd, helper_request_header *hdr, const char* path, helper_response *res) {

    fprintf(stderr, "[Helper] Starting upload to path: %s\n", path);
    int fd = -1;
    if (sandboxUserToHisHome(&hdr->session) == -1) {
        snprintf(res->msg, sizeof(res->msg), "Sandbox error");
        writeAll(server_fd, res, sizeof(*res));
        return;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Open/Create failed: %s", strerror(errno));
        goto out;
        return;
    }
    if (lock_fd(fd, LOCK_EXCLUSIVE) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Lock failed");;
        goto out;
    }
    res->status = 0;
    res->payload_len = 0; 
    snprintf(res->msg, sizeof(res->msg), "Success");
    if (writeAll(server_fd, res, sizeof(helper_response)) < 0) {
        goto out;
    }
    char stream_buf[16384];
    ssize_t n;
    while ((n = read(server_fd, stream_buf, sizeof(stream_buf))) > 0) {
        if (writeAll(fd, stream_buf, n) < 0) {
            fprintf(stderr, "[Helper] Disk write error\n");
            break;
        }
    }
out: 
    if (res->status != 0) {
        writeAll(server_fd, res, sizeof(helper_response));
    }
    if (fd >= 0) {
        unlock_fd(fd);
        close(fd);
    }
    
    if (regainRoot() == -1) {
        _exit(1);
    }
}



void HandleHelperTransfer(int server_fd, helper_request_header *hdr, const char* root, const char* sender, const char* filename, const char* recv, const char* targetPath, helper_response* res) {
    char src_full_path[512];
    char dest_full_path[512];

    int src_fd = -1;
    int dest_fd = -1;

    snprintf(src_full_path, sizeof(src_full_path), "%s/%s/%s", root, sender, filename);
    snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s/%s/%s", root, recv, targetPath, filename);

    // cant chroot so locate substring .. for path traversal
    if (strstr(targetPath, "..") || strstr(filename, "..")) {
        snprintf(res->msg, sizeof(res->msg), "Security Error: Illegal path characters");
        goto out;
    }
    src_fd = open(src_full_path, O_RDONLY);
    if (src_fd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Source file not found");
        goto out;
    }
    if (lock_fd(src_fd, LOCK_SHARED) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Could not lock source file");
        goto out;
    }
    dest_fd = open(dest_full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd < 0) {
        snprintf(res->msg, sizeof(res->msg), "Destination path invalid or permission denied");
        goto out;
    }
    struct passwd *pw = getpwnam(recv);
    if (pw != NULL) {
        if (fchown(dest_fd, pw->pw_uid, pw->pw_gid) != 0) {
            // Log error but attempt to continue
            fprintf(stderr, "[Helper] Warning: Failed to change owner to %s: %s\n", recv, strerror(errno));
        }
    } else {
        res->status = -1;
        snprintf(res->msg, sizeof(res->msg), "Recipient user '%s' does not exist on system", recv);
        goto out;
    }
    if (lock_fd(dest_fd, LOCK_EXCLUSIVE) < 0) {
        snprintf(res->msg, sizeof(res->msg), "Could not lock destination file");
        goto out;
    }
    char buf[8192];
    ssize_t n_read;
    while ((n_read = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dest_fd, buf, n_read) != n_read) {
            snprintf(res->msg, sizeof(res->msg), "Write error during transfer");
            goto out;
        }
    }
    res->status = 0;
    snprintf(res->msg, sizeof(res->msg), "Transfer successful");
out: 
    if (src_fd >= 0) close(src_fd);
    if (dest_fd >= 0) close(dest_fd);

    writeAll(server_fd, res, sizeof(helper_response));
}
