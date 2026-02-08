#ifndef NET_H
#define NET_H

#include <sys/types.h>
int createUnixSocket(const char* root_dir, gid_t groupId);
int connectToHelper();

#endif