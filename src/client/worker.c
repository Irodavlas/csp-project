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

extern int server_socket;
extern pthread_mutex_t lock;
extern pthread_mutex_t response_lock;
extern pthread_cond_t response_cond;
extern pthread_mutex_t bg_lock;

extern volatile int should_exit;
extern volatile int waiting_for_response;
extern volatile int bg_ops_count;
extern volatile int is_writing_content;
extern char global_server_ip[64];


void* backgroundUploadFunc(void* arg) {
    bg_download_args* args = (bg_download_args*)arg;
    int data_socket = -1;
    FILE *fp = NULL;
    
    data_socket = connectToServer(global_server_ip, args->port);
    if (data_socket < 0) {
        pthread_mutex_lock(&lock);
        if (args->is_bg) {
            fprintf(stderr, "\r\033[K[Error]> Upload: Connection to port %d failed\n[Client]> Enter command: ", args->port);
        } else {
            fprintf(stderr, "[Error]> Upload: Connection to port %d failed\n", args->port);
        }
        fflush(stderr);
        pthread_mutex_unlock(&lock);
        goto cleanup;
    }

    fp = fopen(args->dest_path, "rb");
    if (!fp) {
        pthread_mutex_lock(&lock);
        if (args->is_bg) {
            fprintf(stderr, "\r\033[K[Error]> Upload: Cannot open '%s' (%s)\n[Client]> Enter command: ", 
                    args->dest_path, strerror(errno));
        } else {
            fprintf(stderr, "[Error]> Upload: Cannot open '%s' (%s)\n", args->dest_path, strerror(errno));
        }
        fflush(stderr);
        pthread_mutex_unlock(&lock);
        goto cleanup;
    }

    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (writeAll(data_socket, buf, (ssize_t)n) < 0) {
            pthread_mutex_lock(&lock);
            if (args->is_bg) {
                fprintf(stderr, "\r\033[K[Error]> Upload: Network write failed\n[Client]> Enter command: ");
            } else {
                fprintf(stderr, "[Error]> Upload: Network write failed\n");
            }
            fflush(stderr);
            pthread_mutex_unlock(&lock);
            break;
        }
    }

cleanup:
    if (fp) fclose(fp);
    if (data_socket >= 0) close(data_socket);

    pthread_mutex_lock(&bg_lock);
    if (bg_ops_count > 0) bg_ops_count--;
    pthread_mutex_unlock(&bg_lock);
    
    free(args);
    return NULL;
}

void* backgroundDownloadFunc(void* arg) {
    bg_download_args* args = (bg_download_args*)arg;
    int data_socket = -1;
    FILE *fp = NULL;
    
    data_socket = connectToServer(global_server_ip, args->port);
    if (data_socket < 0) {
        pthread_mutex_lock(&lock);
        if (args->is_bg) {
            fprintf(stderr, "\r\033[K[Error]> Download: Connection failed\n[Client]> Enter command: ");
        } else {
            fprintf(stderr, "[Error]> Download: Connection failed\n");
        }
        fflush(stderr);
        pthread_mutex_unlock(&lock);
        goto cleanup;
    }

    fp = fopen(args->dest_path, "wb");
    if (!fp) {
        pthread_mutex_lock(&lock);
        if (args->is_bg) {
            fprintf(stderr, "\r\033[K[Error]> Download: Cannot create '%s'\n[Client]> Enter command: ", args->dest_path);
        } else {
            fprintf(stderr, "[Error]> Download: Cannot create '%s'\n", args->dest_path);
        }
        fflush(stderr);
        pthread_mutex_unlock(&lock);
        goto cleanup;
    }

    char buf[16384];
    ssize_t n;
    while ((n = read(data_socket, buf, sizeof(buf))) > 0) {
        size_t written = fwrite(buf, 1, n, fp);
        if (written < (size_t)n) {
            pthread_mutex_lock(&lock);
            if (args->is_bg) {
                fprintf(stderr, "\r\033[K[Error]> Disk write error on '%s'\n[Client]> Enter command: ", args->dest_path);
            } else {
                fprintf(stderr, "[Error]> Disk write error on '%s'\n", args->dest_path);
            }
            fflush(stderr);
            pthread_mutex_unlock(&lock);
            break; 
        }
    }

    if (n < 0) {
        pthread_mutex_lock(&lock);
        if (args->is_bg) {
            fprintf(stderr, "\r\033[K[Error]> Network interrupted during download\n[Client]> Enter command: ");
        } else {
            fprintf(stderr, "[Error]> Network interrupted during download\n");
        }
        fflush(stderr);
        pthread_mutex_unlock(&lock);
    }

cleanup:
    if (fp) fclose(fp);
    if (data_socket >= 0) close(data_socket);

    pthread_mutex_lock(&bg_lock);
    if (bg_ops_count > 0) bg_ops_count--;
    pthread_mutex_unlock(&bg_lock);
    
    free(args);
    return NULL;
}

void* readThreadFunc(void* arg) {
    while (!should_exit) {
        msg_header resp_hdr;
        // Attempt to read header
        if (readAll(server_socket, &resp_hdr, sizeof(resp_hdr)) <= 0) {
            should_exit = 1;
            pthread_cond_signal(&response_cond); // Wake writer so it can exit
            break;
        }
        
        char *resp_buf = malloc(resp_hdr.payloadLength + 1);
        if (resp_hdr.payloadLength > 0) {
            if (readAll(server_socket, resp_buf, resp_hdr.payloadLength) <= 0) {
                free(resp_buf); 
                should_exit = 1;
                pthread_cond_broadcast(&response_cond);
                break;
            }
        }
        
        resp_buf[resp_hdr.payloadLength] = '\0';

        pthread_mutex_lock(&lock);
        
        printf("\r\033[K");
        

        if (resp_hdr.type == TEXT) {
            if (resp_hdr.is_background) {
                printf("[Background]> %s\n", resp_buf);
                if (is_writing_content) {
                    printf("[Client]> (Still recording file content... hit Ctrl+D to finish)\n");
                } else {
                    printf("[Client]> Enter command: ");
                }
                fflush(stdout);
            } else {
                printf("[Server]> %s\n", resp_buf);
            }
        } else if (resp_hdr.type == LSRES) {
            int num_entries = resp_hdr.payloadLength / sizeof(FileEntry);
            FileEntry *entries = (FileEntry *)resp_buf;
            for (int i = 0; i < num_entries; i++) {
                printf("%-11s %-20s %10ld bytes\n", 
                    entries[i].perms, entries[i].name, (long)entries[i].size);
            }
        } else if (resp_hdr.type == READCMD) {
            if (resp_hdr.payloadLength > 0) {
                printf("[Server]> Content:\n"); 
                fwrite(resp_buf, 1, resp_hdr.payloadLength, stdout);
                printf("\n");
            } else {
                printf("[Server]> File is empty\n");
            }
        }
        else if (resp_hdr.type == DOWNLOAD_RES) {
            bg_download_args *bg_args = malloc(sizeof(bg_download_args));
            // Server sends: "DATA_PORT <port> <dest_path>"
            bg_args->is_bg = resp_hdr.is_background;
            if (sscanf(resp_buf, "DATA_PORT %d %s", &bg_args->port, bg_args->dest_path) == 2) {
                pthread_t bg_tid;
                pthread_create(&bg_tid, NULL, backgroundDownloadFunc, bg_args);
                pthread_detach(bg_tid);
            } else {
                printf("[Error]> Failed to parse download response: %s\n", resp_buf);
                free(bg_args);
            }
        } else if (resp_hdr.type == UPLOAD_RES) {
            bg_download_args *bg_args = malloc(sizeof(bg_download_args));
            bg_args->is_bg = resp_hdr.is_background;
            if (sscanf(resp_buf, "DATA_PORT %d %s", &bg_args->port, bg_args->dest_path) == 2) {
                pthread_t bg_tid;
                pthread_create(&bg_tid, NULL, backgroundUploadFunc, bg_args);
                pthread_detach(bg_tid);
            } else {
                printf("[Error]> Failed to parse upload response\n");
                free(bg_args);
            }
        }

        pthread_mutex_unlock(&lock);
        
        if (!resp_hdr.is_background) {
            if (resp_hdr.type != DOWNLOAD_RES && resp_hdr.type != UPLOAD_RES) {            
                pthread_mutex_lock(&response_lock);
                waiting_for_response = 0;
                pthread_cond_signal(&response_cond);
                pthread_mutex_unlock(&response_lock);
            }
        }
        
        free(resp_buf);
    }
    
    return NULL;
}

void* writeThreadFunc(void* arg) {
    while (!should_exit) {
        // Wait until the previous response is finished OR all bg ops are done
        pthread_mutex_lock(&response_lock);
        while (waiting_for_response && !should_exit) {
            pthread_cond_wait(&response_cond, &response_lock);
        }
        pthread_mutex_unlock(&response_lock);

        if (should_exit) break;

       
        pthread_mutex_unlock(&bg_lock);

        pthread_mutex_lock(&lock);
        printf("[Client]> Enter command: ");
        fflush(stdout);
        pthread_mutex_unlock(&lock);
        
        char command[256];
        if (fgets(command, sizeof(command), stdin) == NULL) {
            should_exit = 1;
            break;
        }
        
        command[strcspn(command, "\n")] = 0;
        if (command[0] == '\0') continue;
        
        if (strcmp(command, "exit") == 0) {
            pthread_mutex_lock(&bg_lock);
            if (bg_ops_count > 0) {
                printf("[Client]> Cannot exit: %d background operation(s) pending\n", bg_ops_count);
                pthread_mutex_unlock(&bg_lock);
                continue;
            }
            pthread_mutex_unlock(&bg_lock);
            should_exit = 1;
            break;
        }

        int is_background = (strstr(command, " -b") != NULL);
        msg_header hdr = { .is_background = (uint8_t)is_background };
        char *payload = NULL;
        uint32_t total_len = 0;

        if (strncmp(command, "write ", 6) == 0) {
            pthread_mutex_lock(&lock);
            printf("[Client]> Enter content (Ctrl+D to finish):\n");
            fflush(stdout);
            is_writing_content = 1;
            pthread_mutex_unlock(&lock);
            
            char *write_buf = NULL;
            size_t write_len = 0;
            char temp[1024];
            ssize_t n;
            
            while ((n = read(STDIN_FILENO, temp, sizeof(temp))) > 0) {
                char *new_ptr = realloc(write_buf, write_len + n);
                if (!new_ptr) break;
                write_buf = new_ptr;
                memcpy(write_buf + write_len, temp, n);
                write_len += n;
            }
            is_writing_content = 0;

            size_t cmd_len = strlen(command) + 1;
            total_len = cmd_len + write_len;
            payload = malloc(total_len);
            memcpy(payload, command, cmd_len);
            if (write_buf) memcpy(payload + cmd_len, write_buf, write_len);
            
            hdr.type = WRITECMD;
            hdr.payloadLength = total_len;
            free(write_buf);
        } else {
            hdr.type = CMDREQ;
            hdr.payloadLength = (uint32_t)strlen(command) + 1;
            payload = malloc(hdr.payloadLength); 
            strcpy(payload, command);
        }

        if (is_background) {
            pthread_mutex_lock(&bg_lock);
            bg_ops_count++;
            pthread_mutex_unlock(&bg_lock);
            
            pthread_mutex_lock(&lock);
            printf("[Client]> Command sent to background...\n");
            pthread_mutex_unlock(&lock);
            
            // Don't wait for response on background commands
            pthread_mutex_lock(&response_lock);
            waiting_for_response = 0;
            pthread_mutex_unlock(&response_lock);
        } else {
            // Wait for response on foreground commands
            pthread_mutex_lock(&response_lock);
            waiting_for_response = 1;
            pthread_mutex_unlock(&response_lock);
        }

        writeAll(server_socket, &hdr, sizeof(hdr));
        writeAll(server_socket, payload, hdr.payloadLength);

        free(payload);
    }
    return NULL;
}
