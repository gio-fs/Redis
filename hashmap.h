#pragma once
#include <cstddef>
#include <cstdint> 

struct HNode {
    HNode* next = NULL;
    uint64_t hash = 0; // intrusive but needs hash
};

struct HTab {
    HNode** slots = NULL;
    size_t mask = 0;
    size_t size = 0;
};

// double HTable for progressive resizing
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;
};
    
HNode* lookupHMap(HMap* map, HNode* key, bool (*eq)(HNode*, HNode*));
HNode* deleteHMap(HMap* map, HNode* key, bool (*eq)(HNode*, HNode*));
void insertHMap(HMap* map, HNode* key);
void helRehashing(HMap* map);
void foreachHMap(HMap* map, bool (*fn) (HNode*, void*), void* arg);
size_t sizeHMap(HMap* map);




