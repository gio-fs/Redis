#include <stdint.h>
#include <stdlib.h>
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>

#define MAX_MSG 4096
#define MAX_LEN MAX_MSG * 8

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


int32_t query(int fd, const std::vector<std::string>& txt) {
    uint32_t len = 4;
    for (const std::string &s : txt) {
	len += 4 + s.size();
    }
    
    if (len > MAX_LEN) {
        printf("too long!\n");
	return -1;
    }

    // send
    printf("sending request with %zu strings, total len: %u\n", txt.size(), len);
    char wbuf[4 + MAX_LEN]; // the header is 4 bytes 
    memcpy(wbuf, &len, 4); // we assume little endian
    uint32_t nstr = txt.size();
    memcpy(&wbuf[4], &nstr, 4);
    printf("request body: "); 
    uint32_t curr = 8;
    for (size_t i = 0; i < txt.size(); i++) {
        const std::string &req = txt[i];
	uint32_t slen = (uint32_t)req.size();
        printf("%.*s ", req.c_str());
	memcpy(&wbuf[curr], &slen, 4);
	memcpy(&wbuf[curr + 4], req.data(), slen);
	curr += 4 + slen;
    }
    printf("\n");
    
    int err = write_full(fd, wbuf, len + 4);
    if (err) {
	msg("write error from client");
	return err;
    }

    printf("request sent\n");
    return 0;
}

static uint32_t get_response(int fd) {
    // read server's header 
    char rbuf[4 + MAX_LEN];
    errno = 0;
    int err = read_full(fd, rbuf, 4);
    if (err) {
	msg(errno == 0? "EOF" : "read error from client");
	return err;
    }
    size_t len = 0;
    memcpy(&len, rbuf, 4);
    
    if (len > MAX_LEN) {
	msg("message is too long (max size is 4096)");
	return -1;
    }

    // server's reply
    errno = 0;
    err = read_full(fd, &rbuf[4], len);
    if (err) {
	msg(errno == 0? "EOF" : "read error from client");
	return err;
    }

    uint32_t res_status = 0;
    if (len < 4) {
	msg("bad response");
	return -1;
    }

    memcpy(&res_status, &rbuf[4], 4);
    // do smt with server's reply
    printf("status: %u, data: '%.*s'\n", res_status, (int)(len - 4), &rbuf[8]);
    return 0;
}
	

int main(int argc, char** argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {}; //IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); //port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // IP 127.0.0.1
    
    int rv = connect(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (rv) {
        die("connect");
    }
    printf("starting query...\n");
    
    std::vector<std::string> query_list {};
    for (int i = 1; i < argc; i++) {
	query_list.push_back(argv[i]);
    }
//    size_t req_count = query_list.size();
//    std::cout << "insert the number of requests: ";
//    std::cin >> req_count;
//    std::cin.ignore();
    
    // for (size_t i = 0; i < req_count; i++) {
// 	std::cout << "insert request: ";
//	getline(std::cin, query_list.emplace_back());
//	std::cout << "\n";
//	int err = query(
//	    fd, 
//	    (uint8_t*)query_list[i].data(), 
//	    query_list[i].size()
//	);
//	if (err) {
//	    goto DONE;
//	}
//    }
//
    int err = query(fd, query_list);
    if (err) {
        goto DONE;
    }

    err = get_response(fd);
    if (err) {
	goto DONE;
    }
   
DONE:
   close(fd);
   printf("done.\n");
   return 0;
}
