#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>





// inet_pton - convert IPv4 and IPv6 addresses from text to binary form
int validate_ipv4(const char* ip) {
    struct in_addr addr;
    return inet_pton(AF_INET, ip, &addr) == 1;
}
int validate_port() {

}


// to send structs over tcp 
ssize_t writeAll(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, (const char*)buf + sent, len - sent);
        if (n <= 0) return -1; 
        sent += n;
    }
    return sent;
}

ssize_t readAll(int fd, void *buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = read(fd, (char*)buf + recvd, len - recvd);
        if (n == 0)
            return 0;      
        if (n < 0)
            return -1;
        recvd += n;
    }
    return recvd;
}

ssize_t readByteStream(int client_fd, char* buffer, size_t size) {
    /* 
    here we could have also defined a static buffer to make it persistent and return a pointer to it.
    static char buffer[BUFFERSIZE]; 
    
    */
    int len = 0;

    while (len < size - 1) {
        // difference between size_T and ssize_t is long unsigned and long, so no errors on size_t
        if (len == size - 1) {
            errno = EMSGSIZE;
            printf("Message was too big to fit inside buffer\n");
            return -1;
        }
        ssize_t n = read(client_fd, &buffer[len], 1);
        
        if (n < 0) {
            if (errno == EINTR)
                continue;              // retry on signal
            perror("read error");
            return -1;                 
        }
        if (n == 0) {                  // client closed
            printf("Client disconected\n");
            return 0;                
        }
        if (buffer[len] == '\n') {
            break;
        }
        len++;
    }
    buffer[len] = '\0';
    return (ssize_t)len;

}


ssize_t writeByteStream(int client_fds, const char* response, size_t len) {
    int sent = 0;
    const char* p = response;
    while (sent < len) {
        // write will try to write up to (len - sent), so up to all msg.
        // if it writes less, then n is returned and the next iteration will take care of it.
        ssize_t n = write(client_fds, p + sent, len - sent); 

        if (n < 0 ) {
            if (errno == EINTR)
                continue;   
            perror("write");
            return -1;
        }
        if (n == 0) {
            printf("Client disconected\n");
            return sent;
        }
        sent += n;

    }
    return sent;
}

int sendMessage(int fd, const char* msg) {
    ssize_t n = writeByteStream(fd, msg, strlen(msg));
    if (n <= 0) {
        fprintf(stderr, "Error writing to client\n");
        return -1;
    }
    return 0;
}

