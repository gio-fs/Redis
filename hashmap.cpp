#include <assert.h>
#include <cstdlib>
#include "hashmap.h"

const size_t max_load_factor = 4;
const size_t init_size = 4;
const size_t k_work = 256;

static void initHTab(HTab* tab, size_t size) {
    assert(size > 0 && ((size - 1) & size) == 0); // check also for ovewrflow
    tab->slots = (HNode**)calloc(size, sizeof(HNode*));
    tab->mask = size - 1;
    tab->size = 0;
}

// no data alloc: intrusive linked lists
static void insertHTab(HTab* tab, HNode* node) {
    size_t pos = node->hash & tab->mask;
    HNode* next = tab->slots[pos];
    node->next = next;
    tab->slots[pos] = node;
    tab->size++;
}

static HNode** lookupHTab(HTab* tab, HNode* key, bool (*eq)(HNode*, HNode*)) {
    if (!tab->slots) {
	return NULL;
    }
    uint32_t pos = key->hash & tab->mask;
    HNode** from = &tab->slots[pos];
    for (HNode* curr; (curr = *from) != NULL; from = &curr->next) {
	// we return from and not curr cause of deletion
	// before: from -> curr (target) -> next
	// during: from -> curr -> next (new target)
	// after: from -> next 
	if (curr->hash == key->hash && eq(curr, key)) {
	    return from;
	}
    }

    return NULL; // not found
}

static HNode* detachHTab(HTab* tab, HNode** from) {
    HNode* node = *from;
    *from = node->next;
    tab->size--;
    return node; // return the deleted node
}

void helpRehashing(HMap* map) {
    size_t nwork = 0;
    while (map->older.size > 0 && nwork < k_work) {
	HNode** from = &map->older.slots[map->migrate_pos];
	if (!*from) {
	    map->migrate_pos++;
	    continue;
	}
	// move the node 
	insertHTab(&map->newer, detachHTab(&map->older, from));
	nwork++;
    }
    
    // if older.slots is not empty, free it 
    if (map->older.size == 0 && map->older.slots) { 
	free(map->older.slots);
	map->older = HTab{};
    }
}

static void triggerRehashingHMap(HMap* map) {
    map->older = map->newer;
    initHTab(&map->newer, (map->newer.mask + 1) * 2);
    map->migrate_pos = 0; // reset after previous resizing
}

HNode* lookupHMap(HMap* map, HNode* key, bool (*eq)(HNode*, HNode*)) {
    helpRehashing(map);
    HNode** from = lookupHTab(&map->newer, key, eq);
    // look in the older if not found, might be searching during resizing
    if (!from) {
	from = lookupHTab(&map->older, key, eq);
    }
    return from ? *from : NULL;
}

HNode* deleteHMap(HMap* map, HNode* key, bool (*eq)(HNode*, HNode*)) {
    // again, check both
    if (HNode **from = lookupHTab(&map->newer, key, eq)) {
	return detachHTab(&map->newer, from);
    }
    if (HNode **from = lookupHTab(&map->older, key, eq)) {
	return detachHTab(&map->older, from);
    }

    return NULL;
}

	    

void insertHMap(HMap* map, HNode* key) {
    if (!map->newer.slots) {
	initHTab(&map->newer, init_size);
    }
    insertHTab(&map->newer, key);
    
    if (!map->older.slots) {
	uint32_t max_size = (map->newer.mask + 1) * max_load_factor;
	if (map->newer.size >= max_size) {
	    triggerRehashingHMap(map);
	}
    }

    helpRehashing(map);
}

static bool foreachHTab(HTab* tab, bool (*fn) (HNode*, void*), void* arg) {
     for (int i = 0; tab->mask != 0 && i <= tab->mask; ++i) {
	for (HNode* curr = tab->slots[i]; curr != NULL; curr = curr->next) {
	    if (!fn(curr, arg)) {
		return false;
	    }
 	}
    }
    return true;
}

void foreachHMap(HMap* map, bool (*fn) (HNode*, void*), void* arg) {
   foreachHTab(&map->newer, fn, arg) && foreachHTab(&map->older, fn, arg);
}

size_t sizeHMap(HMap* map) {
    return map->older.size + map->newer.size;
}
