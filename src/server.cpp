
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <errno.h>
#include <cassert>
#include <memory>

#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include <source_location>

#include "hashmap.h"
#include "avl.h"
#include "zset.h"
#include "common.h"
#include "logger.h"

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

enum {
    T_INIT = 0,
    T_STR = 1,
    T_ZSET = 2
};

struct LookupKey {
    std::string key;
    HNode node;

    LookupKey() {}
    LookupKey(std::string &s) {
        key.swap(s);
        node.hash = hash_str((uint8_t*)key.data(), key.size());
    }
};

struct Entry {
    struct HNode node; // hashmap node
    std::string key {};

    uint32_t type = 0;
    // two possible types
    union { // union to save up memory
        std::string str;
        ZSet zset;
    };

    ~Entry() {
        if (type == T_STR) {
            str.~basic_string();
        } else if (type == T_ZSET) {
            zset_clear(&zset);
        }
    }

    Entry() : type(T_STR) {
        new (&str) std::string();
    }

    Entry(uint32_t t_) : type(t_) {
        // using new placement with address
        if (t_ == T_STR) {
            new (&str) std::string();
        } else if (t_ == T_ZSET) {
            new (&zset) ZSet;
        } 
    }
};

static bool str2dbl(const std::string &s, double &out) {
    char *endp = NULL;
    // endp stops at the first non-number character
    out = strtod(s.c_str(), &endp);
    return endp == s.c_str() + s.size();
}

static bool str2int(const std::string &s, int64_t &out) {
    char *endp = NULL;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str() + s.size();
}

static bool entry_eq(HNode* lhs, HNode* rhs) {
    Entry* left = container_of(lhs, Entry, node);
    LookupKey* right = container_of(rhs, LookupKey, node);
    return left->key == right->key;
}

void die(const char* msg) {
    int err = errno;
    LOG_ERROR(LogMode::Match, "errno", errno);
    abort();
}

void msg(const char* msg) {
    LOG_INFO(msg);
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
    uint32_t ip = client_addr.sin_addr.s_addr; // ip stored in LSBF
    uint32_t port = client_addr.sin_port; // network byte order
    // printf(
    //     "new client %d.%d.%d.%d:%d\n",
    //     ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
    //     htons(port)
    // );
    LOG_INFO_FMT(
        "new client {}.{}.{}.{}:{}",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
        htons(port)
    );

    fd_set_nonblock(connfd); // set the socket to nonblocking mode
    Conn* conn = new Conn(); // create the connection
    conn->fd = connfd;
    conn->want_read = true; // we want to read the client's request
    return conn;
}

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

static size_t out_begin_arr(Buf &out) {
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, 0); // content to be filled by out_end_arr
    return out.size() - 4; // stream size - tag length
}

static void out_end_arr(Buf &out, size_t start, uint32_t len) {
    assert(out[start - 1]); // check the tag before the array start
    LOG_DEBUG(
        LogMode::Match,
        "len", len
    );
    memcpy(&out[start], &len, 4); // copy the length at the start
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
    std::string err("invalid request length");
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

    while (out.size() < nstr) { // read all the commands
    	uint32_t len = 0;
    	if (!read_u32(data, end, len)) {
	        return -1;
	    }
	
	    out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) {
	        return -1;
	    }
    } 

    if (data != end) { // eventual trailing garbage 
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

static void process_get(std::vector<std::string> &cmd, Buf &out) {
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hash = hash_str((uint8_t*)key.key.data(), key.key.size());
    HNode* node = lookupHMap(&kv_store.db, &key.node, &entry_eq);
    if (!node) {
        out_nil(out);
        return;
    }

    Entry* entry = container_of(node, Entry, node);
    if (entry->type != T_STR) {
        return out_err(out, ERR_BAD_ARG, "Key value is not a string");
    }
    assert(entry->str.size() <= MAX_LEN);
    out_str(out, entry->str.data(), entry->str.size()); // append response
}

static void process_set(std::vector<std::string> &cmd, Buf &out) {
    LookupKey entry(cmd[1]);
    HNode* node = lookupHMap(&kv_store.db, &entry.node, &entry_eq);
    if (!node) {
        Entry* ent = new Entry(T_STR);
        ent->node.hash = entry.node.hash;
        ent->key.swap(entry.key);
        ent->str.swap(cmd[2]);
        insertHMap(&kv_store.db, &ent->node);
        out_nil(out);
    } else {
        // if found, get the corresponding entry and update its .val
        Entry* entry = container_of(node, Entry, node);
        if (entry->type != T_STR) {
            return out_err(out, ERR_BAD_ARG, "Key value is not a string");
        }
        entry->str.swap(cmd[2]);
        out_str(out, cmd[2].data(), cmd[2].size()); // return the previous value
    }

}


static void process_del(std::vector<std::string> &cmd, Buf &out) {
    LookupKey entry(cmd[1]);
    HNode* node = deleteHMap(&kv_store.db, &entry.node, &entry_eq);
    if (node) {
        Entry* to_del = container_of(node, Entry, node);
        out_str(out, to_del->str.data(), to_del->str.size()); // return the value deleted
        delete to_del; // deallocate entry 
    }
    return out_nil(out);
}


static bool get_keys(HNode* node, void* arg) {
    Buf &out = *(Buf*)arg;
    Entry* entry = container_of(node, Entry, node);
    out_str(out, entry->key.data(), entry->key.size());
    return true;
}

static void process_keys(std::vector<std::string> &cmd, Buf &out) {
    out_arr(out, (uint32_t)sizeHMap(&kv_store.db));
    foreachHMap(&kv_store.db, &get_keys, (void*)&out);
}

static ZSet* expect_zset(std::string &zset) {
    LookupKey key(zset);
    HNode* hnode = lookupHMap(&kv_store.db, &key.node, &entry_eq);
    if (!hnode) {
        return NULL;
    }

    // get the corresponding entry and zset
    Entry* entry = container_of(hnode, Entry, node);
    LOG_DEBUG(
        LogMode::Match,
        "entry", entry->type
    );
    return entry->type == T_ZSET? &entry->zset : NULL;
}

// zadd key score name
static void process_zadd(std::vector<std::string> &cmd, Buf& out) {
    LookupKey key(cmd[1]);
    HNode* hnode = lookupHMap(&kv_store.db, &key.node, &entry_eq);

    Entry* entry = NULL;
    if (!hnode) {
        entry = new Entry(T_ZSET);
        entry->key.swap(key.key);
        entry->node.hash = key.node.hash;
        insertHMap(&kv_store.db, &entry->node);
    } else {
        entry = container_of(hnode, Entry, node);
        if (entry->type != T_ZSET) {
            return out_err(out, ERR_BAD_ARG, "expected zset as key");
        }
    }

    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expect double");
    }

    bool is_new = zset_insert(&entry->zset, cmd[3].data(), cmd[3].size(), score);
    const std::string &msg = is_new? "added new entry" : "entry updated";
    return out_str(out, msg.data(), msg.size());
}

// zrem zset name
static void process_zrem(std::vector<std::string> &cmd, Buf &out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_ARG, "expect zset as key");
    }

    ZNode* znode = zset_lookup(zset, cmd[2].data(), cmd[2].size());
    return out_int(out, zset_delete(zset, znode)? 0 : 1);
}
// zname key znode
static void process_zname(std::vector<std::string> &cmd, Buf &out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_ARG, "expect zset");
    }
    const std::string &name = cmd[2];
    ZNode* znode = zset_lookup(zset, name.data(), name.size());
    out_str(out, znode->name, znode->len);
    out_dbl(out, znode->score);
}

// zrank key znode
static void process_zrank(std::vector<std::string> &cmd, Buf &out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_ARG, "expect zset");
    }
    const std::string &name = cmd[2];
    ZNode* znode = zset_lookup(zset, name.data(), name.size());
    return out_int(out, avl_rank(&znode->avl_node));
}

// ZQUERY key score name offset limit
static void process_zquery(std::vector<std::string> &cmd, Buf &out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_ARG, "expect zset");
    }

    double score = 0;
    if (!str2dbl(cmd[2], score)) {
        return out_err(out, ERR_BAD_ARG, "expect double");
    }

    int64_t offset = 0, limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit)) {
        return out_err(out, ERR_BAD_ARG, "expect int");
    }

    if (limit <= 0) {
        return out_err(out, ERR_BAD_ARG, "expect limit greater than 0");
    }

    const std::string &name = cmd[3];
    LOG_DEBUG(
        LogMode::Match,
        "zquery name", name
    );

    // seek to the corresponding znode
    ZNode* znode = zset_seekge(zset, score, name.data(), name.size());
    znode = znode_offset(znode, offset); // offset from the node found
    if (!znode) {
        return out_err(out, ERR_NOT_FOUND, "znode not found");
    } 
    LOG_DEBUG(
        LogMode::Match, 
        "znode name", 
        std::string_view(znode->name, znode->len),
        "limit", limit
    );

    // iterate until limit reached
    size_t start = out_begin_arr(out);
    uint32_t i = 0;
    for (; i < limit && znode; i += 1) {
        LOG_DEBUG("finished znode ", i);
        LOG_DEBUG(
            LogMode::Match,
            "len", znode->len,
            "name", 
            std::string_view(znode->name, znode->len)
        );
        out_str(out, znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
    }

    // copy arr len at the start block
    out_end_arr(out, start, i*2); 
    LOG_DEBUG("end");
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
    } else if (cmd.size() == 6 && cmd[0] == "zquery") {
        process_zquery(cmd, out);
    } else if (cmd.size() == 4 && cmd[0] == "zadd") {
        process_zadd(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zrem") {
        process_zrem(cmd, out);
    } else if (cmd.size() == 3 && cmd[0] == "zrank") {
        process_zrank(cmd, out);
    } else {
	    out_err(out, ERR_UNKNOWN, "Unknown command"); // unknown cmd
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
    // printf("client says: len: %d body: ", len);
    // for (std::string &s : cmd) {
	// printf("%s ", s.c_str());
    // }
    LOG_DEBUG(
        LogMode::Match,
        "len", len,
        "body", cmd
    );

    size_t header = 0;
    start_response(conn->outgoing, &header);
    process_req(cmd, conn->outgoing);
    end_response(conn->outgoing, header);
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

void handle_signal() {
    
}

int main() {
    auto file_sink = std::make_unique<FileSink>("log.txt");
    auto console_sink = std::make_unique<ConsoleSink>();
    logger.add_sinks(console_sink, file_sink);
    logger.set_min_level(LogLevel::Debug);
    // logger.disable();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {}; //IPv4 socket address struct
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // htons is short for 'host-to-network short' 
    addr.sin_addr.s_addr = htonl(0); // IP 0.0.0.0, wildcard

    //sockaddr is the generic socket address struct
    int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr)); 
    rv = listen(fd, SOMAXCONN); // 2nd arg is the size of the connections queue (4096 on linux)
    if (rv) die("listen");
    LOG_INFO("Started listening...");

    // since an fd is allocated as the smallest avaiable non negative
    // integer, an array is sufficient towhat did you change map fd and the respective conn
    std::vector<Conn*> fd2conn {};

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
                    // printf(
                    // 	"closing connection %d.%d.%d.%d:%d\n",
                    // 	ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
                    // 	htons(client_addr.sin_port)
                    // );

                    LOG_INFO_FMT(
                        "closing connection {}.{}.{}.{}:{}",
                        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
                        htons(client_addr.sin_port)
                    );
                } else {
                    LOG_ERROR(
                        "Could not gain connection info, still proceding with deletion"
                    );		  
                }  

                close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }

            // there could be some application logic handling errors
            // but here i just close the connection
        }
    }

}
