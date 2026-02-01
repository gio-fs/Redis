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

#define MAX_MSG 1000000
#define MAX_LEN MAX_MSG * 8

enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5
};

enum {
    ERR_TOO_BIG = 0,
    ERR_UNKWNOWN = 1,
};

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


static int print_response(uint8_t* res, size_t len) {
    if (len < 1) {
	msg("bad response");
	return -1;
    }
    switch (res[0]) {
 	case TAG_NIL:
	    printf("(nil)\n");
	    return 1;
	case TAG_ERR:
	    if (len < 1 + 8) {
		msg("bad response");
		return -1;
	    } 
	{
	    uint32_t err = 0;
	    uint32_t strlen = 0;
	    memcpy(&err, &res[1], 4);
	    memcpy(&strlen, &res[1 + 4], 4);
	    if (strlen > MAX_LEN) {
		msg("response is too long");
		return -1;
	    }
	    printf("(err) %d, %.*s\n", err, strlen, &res[1 + 8]);
	    return 1 + 8 + strlen;
	}
	case TAG_STR:
	    if (len < 1 + 4) {
		msg("bad response");
		    return -1;
	    }	      
	{
	    uint32_t strlen = 0;
	    memcpy(&strlen, &res[1], 4);
	    if (strlen > MAX_LEN) {
		msg("response is too long");
		    return -1;
	    }
	    printf("(str) %.*s\n", strlen, &res[1 + 4]);
	    return 1 + 4 + strlen;
	}
	case TAG_INT: 
	    if (len < 1 + 8) {
		msg("bad response");
		return -1;
	    }
	{
	    int64_t val = 0;
	    memcpy(&val, &res[1], 8);
	    printf("(int) %ld\n", val);
	    return 1 + 8;
	}
	case TAG_DBL: 
	    if (len < 1 + 8) {
		msg("bad response");
		return -1;
	    }
	{
	
	    double val = 0;
	    memcpy(&val, &res[1], 8);
	    printf("(dbl) %g\n", val);
	    return 1 + 8;
	}
	case TAG_ARR: {
	    if (len < 1 + 4) {
		msg("bad response");
		return -1;
	    }
	    uint32_t dim = 0;
	    uint32_t next_bytes = 1 + 4;
	    memcpy(&dim, &res[1], 4);
	    if (dim > MAX_LEN) {
		msg("response is too long");
		return -1;
	    }
	    printf("(arr) [\n");
	    for (int i = 0; i < dim; ++i) {
		printf("\t");
		size_t bytes_left = len - next_bytes;
		uint32_t rv = print_response(&res[next_bytes], bytes_left);
		if (rv > MAX_LEN) {
		    msg("\nresponse is too long");
		    return -1;
		}
		next_bytes += rv;
	    }
	    printf(" ]\n");
	    return next_bytes + 1 + 4;
	}

	default: return -1;

    }
}

	    
static int get_response(int fd) {
    char rbuf[4 + MAX_LEN];
  
    // respose len
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

    // response value
    errno = 0;
    err = read_full(fd, &rbuf[4], len);
    if (err) {
	msg(errno == 0? "EOF" : "read error from client");
	return err;
    }

    int rv = print_response((uint8_t*)&rbuf[4], len);
    if (rv < 0) {
	return rv;
    }

    return 0;
}
	

int main(int argc, char** argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {}; // IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // port
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
