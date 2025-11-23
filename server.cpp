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

void die(const char* msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void msg(const char* msg) {
    fprintf(stderr, "%s\n", msg);
}


// we need full read and write because they depend on the remote peer that
// is sending data: they might be called when the buffer does not contain
// the complete message, thus we iterate through to avoid this situation 
// (the read/write call blocks until the buffer is filled with other portions of data)
int32_t read_full(int fd, char* buf, size_t n) {
    while (n > 0) {
        size_t rv = read(fd, buf, n);
	if (rv <= 0) {
          return -1;
        }
        n -= rv;
        buf += rv;
    } 

    return 0;
} 


int32_t write_full(int fd, const char* buf, size_t n) {
    while (n > 0) {
	size_t rv = write(fd, buf, n);
	if (rv <= 0) {
	    return -1;
	}
	n -= rv;
	buf += rv;
    }
    return 0;
}
 
// connfd is short for 'connection file descriptor'
static uint32_t one_req(int connfd) {
    char rbuf[4 + MAX_LEN] = {}; // 4 bytes header + msg size
    errno = 0;
    ssize_t err = read_full(connfd, rbuf, 4); // reads header

    if (err) {
	// errno is 0 only if syscall succeded: we distinguish the read error case and the EOF
	// since we return -1 for both cases
        msg(errno == 0 ? "EOF in one_req()" : "read_full() error in one_req()");
        return -1;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > MAX_LEN) {
    	fprintf(stderr, "message is too long (%d bytes)\n", len);
	return -1;
    }

    // body of req
    errno = 0;
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg(errno == 0 ? "EOF in one_req()" : "read_full() error in one_req()");
        return -1;
    }

    printf("client says: %s\n", rbuf + 4);
    
    // reply with same protocol
    const char ans[] = " dio traballino";
    char wbuf[4 + sizeof(ans)] = {};
    len = strlen(ans);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], ans, len);
    
    //read/write is the same as recv/send with the latter passing also a flag

    return write_full(connfd, wbuf, len + 4);
}   

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {}; //IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // htons is short for 'host-to-network short' 
    addr.sin_addr.s_addr = htonl(0); // IP 0.0.0.0
    int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr)); //sockaddr is the generic socket address struct

    rv = listen(fd, SOMAXCONN); //2nd arg is the size of the connections queue (4096 on linux)
    if (rv) die("listen");

    while (true) {            
	//accept connections
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        //accepts the connection on top of the connection queue and creates
        //a new connected socket. Returns a new file descriptor referring
        //to that socket
        int connfd = accept(fd, (sockaddr*)&client_addr, &addrlen);
        if (connfd < 0) {
            continue;
        }

	while (true) {
	  errno = 0;
	  int32_t err = one_req(connfd);
	   if (err) {
            msg(errno == 0 ? "EOF in one_req()" : "read_full() error in one_req()");
            break;
           }
	}
        close(connfd);
    }
}
