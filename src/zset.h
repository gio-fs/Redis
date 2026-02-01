#pragma once
#include <cstddef> 
#include "avl.h"
#include "hashmap.h"

// add or remove -> hmap & tree insert/delete
// query by name -> hmap lookup
// seek by score & name -> tree lookup

struct ZSet {
    AVLNode* root = NULL;
    HMap map;
};

// each hashamp entry has an indipendent zset
// making ranking, iterating and multi indexing 
// efficient

struct ZNode {
    // nodes
    AVLNode avl_node;
    HNode hnode;

    // score, name
    double score = 0;

    // flexible arr member w len 0 can use 
    // any extra free space (determined by len)
    size_t len = 0;
    char name[0]; 
};

struct HKey {
    HNode node;
    const char* name = NULL;
    size_t len = 0;
};

bool zset_insert(ZSet* zset, const char* name, size_t len, double score);
ZNode* zset_lookup(ZSet* zset, const char* name , size_t len);
bool zset_delete(ZSet* zset, ZNode* znode);
void zset_clear(ZSet* zset);
bool zless(AVLNode* lhs, AVLNode* rhs);
void avl_insert(ZSet* zset, ZNode* znode);
ZNode* zset_seekge(ZSet* zset, double score, const char* name, size_t len);
ZNode* znode_offset(ZNode* znode, int64_t offset);
