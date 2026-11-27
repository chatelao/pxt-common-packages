#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_BROADCAST 6
#define INADDR_ANY ((in_addr_t)0x00000000)
#define MSG_DONTWAIT 0x40
#define FIONREAD 0x541B

static inline uint16_t lwip_htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint32_t lwip_htonl(uint32_t v) {
    return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
}
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#define htons(x) lwip_htons(x)
#define ntohs(x) lwip_htons(x)
#define htonl(x) lwip_htonl(x)
#define ntohl(x) lwip_htonl(x)

int mock_socket(int domain, int type, int protocol);
int mock_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int mock_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int mock_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int mock_send(int sockfd, const void *buf, size_t len, int flags);
int mock_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int mock_recv(int sockfd, void *buf, size_t len, int flags);
int mock_close(int fd);
int mock_ioctl(int fd, unsigned long req, ...);
int mock_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void mock_freeaddrinfo(struct addrinfo *res);

#define socket mock_socket
#define setsockopt mock_setsockopt
#define bind mock_bind
#define connect mock_connect
#define send mock_send
#define sendto mock_sendto
#define recv mock_recv
#define close mock_close
#define ioctl mock_ioctl
#define getaddrinfo mock_getaddrinfo
#define freeaddrinfo mock_freeaddrinfo

#ifdef __cplusplus
}
#endif
