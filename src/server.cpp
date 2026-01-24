
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <errno.h>
#include <cassert>

#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include <functional>

#include "hashmap.h"

#define MAX_LEN 4096
#define container_of(data, T, member) \
	((T*)((char*)data - offsetof(T, member)))

// keeps connection context between loops
typedef std::vector<uint8_t> Buf;
struct Conn {
    int fd = -1;
    // intention of the application
    bool want_read = false; // POLLIN
    bool want_write = false; // POLLOUT
    bool want_close = false; // POLLERR
	
    Buf incoming {}; // data to be parsed
    Buf outgoing {}; // responses to write to the socket
};


struct {
    HMap db;
} kv_store;

struct Entry {
    struct HNode node;
    std::string key {};
    std::string val {};
};

static bool entry_eq(HNode* lhs, HNode* rhs) {
    struct Entry* left = container_of(lhs, struct Entry, node);
    struct Entry* right = container_of(rhs, struct Entry, node);
    return left->key == right->key;
}

void die(const char* msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void msg(const char* msg) {
    fprintf(stderr, "%s\n", msg);
}

void fd_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // fcntl does some operation on fd
    flags |= O_NONBLOCK; // set the non blocking flag
    fcntl(fd, F_SETFL, flags); // set flags to fd's flags
}

// application logic for accept()
static Conn *handle_accept(int fd) {
    struct sockaddr_in client_addr {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr*)&client_addr, &addrlen);
    if (connfd < 0) {
	msg("failed to accept a connection");
	return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    uint32_t port = client_addr.sin_port;
    printf(
	"new client %d.%d.%d.%d:%d\n",
	ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
	htons(port)
    );

    fd_set_nonblock(connfd); // set the socket to nonblocking mode
    Conn* conn = new Conn(); // create the connection
    conn->fd = connfd;
    conn->want_read = true; // we want to read the client's request
    return conn;
}

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
    ERR_UNKNOWN = 1
};

static void buf_append(Buf &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t len) {
    buf.erase(buf.begin(), buf.begin() + len);
}
static void buf_append_u8(Buf &buf, uint8_t data) {
    buf.push_back(data);
}

static void buf_append_u32(Buf &buf, uint32_t data) {
    buf_append(buf, (const uint8_t*)&data, 4);
}

static void buf_append_i64(Buf &buf, int64_t data) {
    buf_append(buf, (const uint8_t*)&data, 8);
}

static void out_nil(Buf &out) {
    buf_append_u8(out, TAG_NIL);
}

static void out_err(Buf &out, uint32_t err, std::string &&data) {
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, err);
    buf_append_u32(out, (uint32_t)data.size());
    buf_append(out, (const uint8_t*)data.data(), (uint32_t)data.size());
}

static void out_str(Buf &out, const char* data, size_t size) {
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    buf_append(out, (const uint8_t*)data, (uint32_t)size);
}

static void out_int(Buf &out, int64_t data) {
    buf_append_u8(out, TAG_INT);
    buf_append_i64(out, data);
}

static void out_dbl(Buf &out, double data) {
    buf_append_u8(out, TAG_DBL);
    buf_append(out, (const uint8_t*)&data, 8);
}

static void out_arr(Buf &out, uint32_t dim) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, dim);
}

static bool read_u32(const uint8_t* &curr, const uint8_t* end, 
		uint32_t &out) {
    if (curr + 4 > end) {
	return false;
    }
    memcpy(&out, curr, 4);
    curr += 4;
    return true;
}

static bool read_str(const uint8_t* &curr, const uint8_t* end,
		size_t n, std::string &out) {
    if (curr + n > end) {
	return false;
    }

    out.assign(curr, curr + n);
    curr += n;
    return true;
}

static bool handle_invalid_len(Conn* conn) {
    std::string err = "invalid request length";
    size_t size = err.size();
    buf_append(conn->outgoing, (const uint8_t*)&size, 4);
    buf_append(
	conn->outgoing, 
	(const uint8_t*)err.data(), 
	err.size()	
    );
    buf_consume(conn->incoming, 4);
    conn->want_close = true;
    return false;
}

static int32_t parse_req(const uint8_t *data, size_t size, 
		std::vector<std::string> &out) {
    const uint8_t* end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(data, end, nstr)) {
  	    return -1;
    }
    if (nstr > MAX_LEN) {
        return -1;
    }

    while (out.size() < nstr) {
    	uint32_t len = 0;
    	if (!read_u32(data, end, len)) {
	        return -1;
	    }
	
	    out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) {
	        return -1;
	    }
    } 


    if (data != end) {
	    return -1;
    }
    return 0;
}

static size_t response_size(Buf &out, size_t header) {
    return out.size() - header - 4; // response size - header pos - len size
}

static void start_response(Buf &out, size_t* header) {
     *header = out.size();
    buf_append_u32(out, 0); // len
}

static void end_response(Buf &out, size_t header) {
    uint32_t len = (uint32_t)response_size(out, header); 
    if (len > MAX_LEN) {
	out.resize(header + 4);
	out_err(out, ERR_TOO_BIG, "response size is too big");
	len = (uint32_t)response_size(out, header);
    }
    memcpy(&out[header], &len, 4);
}

static uint64_t hash_str(const uint8_t *data, size_t len) { 
    uint64_t hash = 0x8119C9DC5;
    for (size_t i = 0; i < len; i++) {
	hash = (hash + data[i]) * 0x01000193;
    }

    return hash;
}

static void process_get(std::vector<std::string> &cmd, Buf &out) {
    Entry entry;
    entry.key.swap(cmd[1]);
    entry.node.hash = hash_str((uint8_t*)entry.key.data(), entry.key.size());
    HNode* node = lookupHMap(&kv_store.db, &entry.node, &entry_eq);
    if (!node) {
	out_nil(out);
	return;
    }

    const std::string &val = container_of(node, Entry, node)->val;
    assert(val.size() <= MAX_LEN);
    out_str(out, val.data(), val.size()); // append response
}

static void process_set(std::vector<std::string> &cmd, Buf &out) {
    Entry entry;
    entry.key.swap(cmd[1]);
    entry.node.hash = hash_str((uint8_t*)entry.key.data(), entry.key.size());
    HNode* node = lookupHMap(&kv_store.db, &entry.node, &entry_eq);
    if (!node) {
	Entry* ent = new Entry();
	ent->node.hash = entry.node.hash;
	ent->key.swap(entry.key);
	ent->val.swap(cmd[2]);
	insertHMap(&kv_store.db, &ent->node);
	out_nil(out);
    } else {
	// if found, get the corresponding entry and update its .val
	container_of(node, Entry, node)->val.swap(cmd[2]);
	out_str(out, cmd[2].data(), cmd[2].size());
    }

}


static void process_del(std::vector<std::string> &cmd, Buf &out) {
    Entry entry;
    entry.key.swap(cmd[1]);
    entry.node.hash = hash_str((uint8_t*)entry.key.data(), entry.key.size());
    HNode* node = deleteHMap(&kv_store.db, &entry.node, &entry_eq);
    if (node) {
	Entry* to_del = container_of(node, Entry, node);
	out_str(out, to_del->val.data(), to_del->val.size()); // return the value deleted
	delete to_del; // deallocate entry 
    }
}


static bool get_keys(HNode* node, void* arg) {
    Buf &out = *(Buf*)arg;
    std::string &key = container_of(node, Entry, node)->key;
    out_str(out, key.data(), key.size());
    return true;
}

static void process_keys(std::vector<std::string> &cmd, Buf &out) {
    out_arr(out, (uint32_t)sizeHMap(&kv_store.db));
    foreachHMap(&kv_store.db, &get_keys, (void*)&out);
}


static void process_req(std::vector<std::string> &cmd, Buf &out) {
    if (cmd.size() == 2 && cmd[0] == "get") {
	process_get(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "set") { 	
	process_set(cmd, out);
    } else if (cmd.size() == 2 && cmd[0] == "del") {
	process_del(cmd, out);
    } else if (cmd.size() == 1 && cmd[0] == "keys"){  
	process_keys(cmd, out);
    } else {
        std::string msg = "Unknown command";
	    out_err(out, ERR_UNKNOWN, std::move(msg)); // unknown cmd
    }
}
        
 
static bool try_one_req(Conn* conn) {
    // try to parse the incoming data. If size is smaller than
    // 4, the header is not completely arrived yet so we
    // save the application intention (want to read) and return
    // false
    if (conn->incoming.size() < 4) {
    	return false;
    }

    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if (len > MAX_LEN || len <= 0) {
	    return handle_invalid_len(conn);
    }

    // message body
    if (len + 4 > conn->incoming.size()) { // message is not complete yet
	    return false;
    } 
    // parse the request. len is the length of the request, 
    // so the structure of a message is as follows:
    // ++++++++++++++++++++++++++++++++++++++++++++++++++++
    // | len | nstr | strlen | str1 | ... | strlen | strn |
    // ++++++++++++++++++++++++++++++++++++++++++++++++++++
    // 	     |____________________________________________|
    // 	     	        request of length 'len'
    const uint8_t* req = &conn->incoming[4]; // start of the request
    std::vector<std::string> cmd {};
    if (parse_req(req, len, cmd) < 0) {
	    conn->want_close = true;
	    return false;
    } 
    printf("client says: len: %d body: ", len);
    for (std::string &s : cmd) {
	printf("%s ", s.c_str());
    }

    size_t header = 0;
    start_response(conn->outgoing, &header);
    process_req(cmd, conn->outgoing);
    end_response(conn->outgoing, header);

    printf("\n");
    buf_consume(conn->incoming, len + 4); // consume the parsed req 
    return true;
}

static void handle_read(Conn* conn) {
    // nonblocking read
    uint8_t buf[32 * 1024]; 
    ssize_t bytes_read = read(conn->fd, buf, sizeof(buf)); 	
    if (bytes_read < 0 && errno == EAGAIN) {
    	return; // not ready
    }
    if (bytes_read <= 0) {
        conn->want_close = true;
	    return; 
    }

    // add data to incoming buffer
    buf_append(conn->incoming, buf, (size_t)bytes_read);
    while (try_one_req(conn)) {}; // pipelined requests

    if (conn->outgoing.size() > 0) { // server has response, want_write -> true
    	conn->want_read = false;
	conn->want_write = true;
    } // else want read
} 

static void handle_write(Conn* conn) {
    if (conn->outgoing.size() < 0) {
	    msg("fatal: handle_write: outgoing size less than 0");
	    abort();
    }

    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
	    return; // not ready
    }
    if (rv < 0) {
        conn->want_close = true;
	    return;
    }

    buf_consume(conn->outgoing, rv); // consume the response
    if (conn->outgoing.size() == 0) { // if all data has been written
	    conn->want_read = true;
	    conn->want_write = false;
    } // else want write
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {}; //IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // htons is short for 'host-to-network short' 
    addr.sin_addr.s_addr = htonl(0); // Iwhat did you changeP 0.0.0.0

    //sockaddr is the generic socket address struct
    int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr)); 
    rv = listen(fd, SOMAXCONN); // 2nd arg is the size of the connections queue (4096 on linux)
    if (rv) die("listen");

    // since an fd is allocated as the smallest avaiable non negative
    // integer, an array is sufficient towhat did you change map fd and the respective conn
    std::vector<Conn *> fd2conn {};

    // event loop
    std::vector<struct pollfd> poll_args {};
    while (true) {            
	poll_args.clear();

	struct pollfd pfd = {fd, POLLIN, 0}; // listening socket
	poll_args.push_back(pfd);
	
	// traverse the connections
	for (Conn *conn : fd2conn) {
	    if (!conn) {
		    continue;
	    }
	    struct pollfd pfd = {conn->fd, POLLERR, 0}; // we set to POLLERR by default
   	    // POLLIN -> read, POLLOUT -> write
	    if (conn->want_read) {
	    	pfd.events |= POLLIN;
	    }
	    if (conn->want_write) {
	    	pfd.events |= POLLOUT;
	    }

	    poll_args.push_back(pfd);
	}
	
	// only blocking syscall: we pass to poll our application's intentions
	// and it returns the first avaiable socket
	int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1); // no timeout
  	if (rv < 0 && errno == EINTR) { // in case process receives interrupt during call
	    continue;
	}

	if (rv < 0) {
	    die("poll()");
	}

	// accept new connections, handle the listening socket
	// accept() is treated as POLLIN in poll()
	if (poll_args[0].revents) { // check for responses
	    if (Conn *conn = handle_accept(fd)) {
	    	if (fd2conn.size() <= (size_t)conn->fd) {
	    	    fd2conn.resize(conn->fd + 1);
	    	}
	    	fd2conn[conn->fd] = conn; // map the connection to its fd
	    }
	}

	// handle the connection sockets
	for (size_t i = 1; i < poll_args.size(); i++) {
	    uint32_t readiness = poll_args[i].revents;
	    Conn *conn = fd2conn[poll_args[i].fd];

	    // check the flags and handle respectively
	    if (readiness & POLLIN) {
	    	handle_read(conn);
	    }
	    if (readiness & POLLOUT) {
	    	handle_write(conn);
	    }
	    
	    // close the socket if POLLERR or from app logic
	    if ((readiness & POLLERR) || conn->want_close) {
		struct sockaddr_in client_addr;
		socklen_t size = sizeof(struct sockaddr_in);
		int rv = getpeername(conn->fd, (struct sockaddr*)&client_addr, &size);	
		uint32_t ip = client_addr.sin_addr.s_addr;
		if (!rv) {
		    printf(
		    	"closing connection %d.%d.%d.%d:%d\n",
		    	ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
		    	htons(client_addr.sin_port)
		    );
		} else {
		    printf("couldn't get connection info, still proceding with deletion");		  }  

		close(conn->fd);
	    	fd2conn[conn->fd] = NULL;
	    	delete conn;
	    }
	    // there could be some application logic handling errors
	    // but here i just close the connection
	}
    }
}
