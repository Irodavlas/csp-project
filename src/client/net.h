#ifndef NET_H
#define NET_H

#include "common/utility.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
typedef struct {
    char* CommandName; 
} CommandRequest;
int connectToServer(char *ip, int port);
#endif