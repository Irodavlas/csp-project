#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include "common/utility.h"

#include "net.h"
#include "worker.h"
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8080

int server_socket;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // stdout mutex
pthread_mutex_t response_lock = PTHREAD_MUTEX_INITIALIZER; // manages the waiting for response flag
pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER; // cond to wake up threads 
pthread_mutex_t bg_lock = PTHREAD_MUTEX_INITIALIZER; // lock for background operations 

volatile int should_exit = 0;
volatile int waiting_for_response = 0;
volatile int bg_ops_count = 0; // count to avoid early exit
volatile int is_writing_content = 0;

char global_server_ip[64]; 

int main(int argc, char* argv[]) {
    if (argc > 3) {
        printf("Usage: %s [IP] [Port]\n", argv[0]);
        return -1;
    }
    
    char* ip = DEFAULT_IP;
    int port = DEFAULT_PORT;
    
    if (argc >= 2 && argv[1][0] != '\0') {
        ip = argv[1];
        if (!validate_ipv4(ip)) {
            printf("> IP: %s not valid\n", ip);
            return -1;
        }
    }

    if (argc >= 3 && argv[2][0] != '\0') {
        port = atoi(argv[2]);
    }
    
    strncpy(global_server_ip, ip, sizeof(global_server_ip) - 1);
    global_server_ip[sizeof(global_server_ip) - 1] = '\0';

    server_socket = connectToServer(ip, port);
    if (server_socket == -1) return -1;

    printf("[Client]> Successfully connected to server\n");
    
    pthread_t read_thread, write_thread;
    pthread_create(&read_thread, NULL, readThreadFunc, NULL);
    pthread_create(&write_thread, NULL, writeThreadFunc, NULL);
    
    pthread_join(write_thread, NULL);
    pthread_cancel(read_thread); 
    pthread_join(read_thread, NULL);
    
    close(server_socket);
    return 0;
}