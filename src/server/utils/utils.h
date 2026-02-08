#ifndef UTILS_H
#define UTILS_H

#include <limits.h>
#include <stddef.h>
#include <stdbool.h> 
#include <sys/types.h> 

#include "handler/handlers.h"



bool isUsernameValid(char* username);
int createUserDirectory(const char* pathname, uid_t uid, gid_t gid, mode_t mode);

int dropPrivilegesTemp(const ClientSession *cs);
int regainRoot();

int sandboxUserToHisHome(const ClientSession* session);
int sandboxUserToRoot(const ClientSession* session, char* rootdir);

int acquireUserCreationLock(const char* lock_file_path);
void releaseUserCreationLock(int fd);

#endif