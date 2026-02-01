#include <cstddef> 
#include <memory.h>
#include <cassert>

#include "zset.h"
#include "hashmap.h"
#include "avl.h"
#include "common.h"
#include "logger.h"
#include <cstdlib>
#define min(a, b) (a < b? a : b)
#define container_of(data, T, member) \
	((T*)((char*)data - offsetof(T, member)))

static ZNode* znode_new(const char* name, size_t len, double score) {
    // manual alloc needed for the flexible arr member
    ZNode* node = (ZNode*)malloc(sizeof(ZNode) + len);
    avl_init(&node->avl_node);
    node->hnode.next = NULL;
    node->hnode.hash = hash_str((uint8_t*)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

static bool hcmp(HNode* node, HNode* key) {
    ZNode* znode = container_of(node, ZNode, hnode);
    HKey* hkey = container_of(key, HKey, node);

    // if size is not equal they're of course
    // different nodes, else we compare each char
    if (znode->len != hkey->len) {
        return false;
    }

    return memcmp(znode->name, hkey->name, hkey->len) == 0;
}

ZNode* zset_lookup(ZSet* zset, const char* name, size_t len) {
    if (!zset->root) {
        return NULL;
    }

    // searching for the key, returning the
    // corresponding znode
    HKey key {};
    key.node.hash = hash_str((uint8_t*)name, len);
    key.name = name;
    key.len = len;
    HNode* found = lookupHMap(&zset->map, &key.node, &hcmp);
    return found? container_of(found, ZNode, hnode) : NULL;
}

bool zless(AVLNode* lhs, AVLNode* rhs) {
    ZNode* zr = container_of(rhs, ZNode, avl_node);
    ZNode* zl = container_of(lhs, ZNode, avl_node);
    if (zr->score != zl->score) {
        return zl->score < zr->score;
    }

    if (zr->len != zl->len) {
        return zl->len < zr->len;
    }

    return false;
}


bool zless(AVLNode* node, double score, const char* name, size_t len) {
    ZNode* znode = container_of(node, ZNode, avl_node);
    if (znode->score != score) {
        return znode->score < score;
    }

    int rv = memcmp(znode->name, name, min(znode->len, len));
    return rv < 0? true : false;
}

static void zset_update(ZSet* zset, ZNode* znode, double score) {
    if (score == znode->score) {
        return;
    }
    zset->root = avl_del(&znode->avl_node); 
    // create new updated znode and insert again
    avl_init(&znode->avl_node);
    znode->score = score;
    avl_insert(zset, znode);
}

bool zset_insert(ZSet* zset, const char* name, size_t len, double score) {
    if (ZNode* node = zset_lookup(zset, name, len)) {
        zset_update(zset, node , score);
        return false;
    }
    ZNode* znode = znode_new(name, len, score);
    LOG_DEBUG(
        LogMode::Match,
        "name",
        std::string_view(znode->name, znode->len),
        "znode->len", znode->len
    );
    insertHMap(&zset->map, &znode->hnode);
    avl_insert(zset, znode);
    LOG_DEBUG("end");
    return true;
}

static void znode_del(ZNode* node) {
    free(node);
}

bool zset_delete(ZSet* zset, ZNode* znode) {
    HKey key;
    key.node.hash = znode->hnode.hash;
    key.name = znode->name;
    key.len = znode->len;
    HNode* found = deleteHMap(&zset->map, &key.node, &hcmp);
    assert(found);

    // delete and update the root
    zset->root = avl_del(&znode->avl_node);
    znode_del(znode);
    return true;
}

// (seek greater equal)
ZNode* zset_seekge(ZSet* zset, double score, const char* name, size_t len) {
    AVLNode* found = NULL;
    for (AVLNode* node = zset->root; node;) {
        if (zless(node, score, name, len)) {
            node = node->right; // key is greater
        } else {
            found = node; // possible candidate
            node = node->left;
        }
    }
    
    return found? container_of(found, ZNode, avl_node) : NULL;
}

// walk into the tree by some integer offset
ZNode* znode_offset(ZNode* znode, int64_t offset) {
    AVLNode* node = znode? avl_offset(&znode->avl_node, offset) : NULL;
    return node? container_of(node, ZNode, avl_node) : NULL;
}

void avl_clear(AVLNode* node) {
    if (!node) {
        return;
    }

    avl_clear(node->right);
    avl_clear(node->left);
    znode_del(container_of(node, ZNode, avl_node));
}

void zset_clear(ZSet* zset) {
    avl_clear(zset->root);
    hmap_clear(&zset->map);
    *zset = ZSet{};
}