#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#define MAX_LEN 4096

//this is really what the socket API is trying to achieve
struct sane_sockaddr {
    uint16_t family; //either AF_INET or AF_INET6
    uint16_t port;
    union {
        struct {uint8_t ipv4[4]; };
        struct {uint8_t ipv6[16]; /* ... */};
    };

};

void die(const char* msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void msg(const char* msg) {
    fprintf(stderr, "%s\n",  msg);
}

int read_full(int fd, char* buf, size_t n) {
    while (n > 0) {
	int rv = read(fd, buf, n);
	if (rv <= 0) {
	    return -1;
	}
	n -= rv;
	buf += rv;
    }
   return 0;
}

int write_full(int fd, const char* buf, size_t n) {
    while (n > 0) {
	int rv = write(fd, buf, n);
	if (rv <= 0) {
	    return -1;
	}
	n -= rv;
	buf += rv;
    }
   return 0;
}


int32_t query(int fd, const char* txt) {
    uint32_t len = (uint32_t)strlen(txt);
    if (len > MAX_LEN) {
	return -1;
    }

    // send
    printf("sending request... (len is %d)\n", len);
    char wbuf[4 + MAX_LEN]; // the header is 4 bytes 
    memcpy(wbuf, &len, 4), // we assume little endian
    memcpy(&wbuf[4], txt, len);
    int err = write_full(fd, wbuf, len + 4);
    if (err) {
	msg("write error from client");
	return err;
    }

    printf("request sent\n");
    
    // read server's header 
    char rbuf[4 + MAX_LEN];
    errno = 0;
    err = read_full(fd, rbuf, 4);
    if (err) {
	msg(errno == 0? "EOF" : "read error from client");
	return err;
    }
    memcpy(&len, rbuf, 4); // we reuse 'len' for server's msg length
    if (len > MAX_LEN) {
	msg("message is too long (max size is 4096)");
	return -1;
    }

    // server's reply 
    errno = 0;
    err = read_full(fd, rbuf, len);
    if (err) {
	msg(errno == 0? "EOF" : "read error from client");
	return err;
    }
    
    // do smt with server's reply
    printf("server says:  %.*s\n", len, &rbuf[4]);
    return 0;
}
	

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket");
    }

    struct sockaddr_in addr = {}; //IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234); //port
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // IP 127.0.0.1
    int rv = connect(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rv) {
        die("connect");
    }
    printf("starting queries...\n");

   int err = query(fd, "hhhhh");
   if (err) {
       goto DONE;
   }
   err = query(fd, "query2");
   if (err) {
       goto DONE;
   }

DONE:
   close(fd);
   return 0;
}
