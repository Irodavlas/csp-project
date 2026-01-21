#include <arpa/inet.h>

// inet_pton - convert IPv4 and IPv6 addresses from text to binary form
int validate_ipv4(const char* ip) {
    struct in_addr addr;
    return inet_pton(AF_INET, ip, &addr) == 1;
}
int validate_port() {

}