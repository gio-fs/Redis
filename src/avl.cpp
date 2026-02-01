#include <assert.h>
#include <cstddef>

#include "avl.h"
#include "zset.h"
#include "logger.h"
#define max(a, b) (a > b? b : a)
#define abs(a) (a < 0? -a : a)


void avl_init(AVLNode* node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    node->cnt = 1;
}

static size_t avl_height(AVLNode* node) {
    return node? node->height : 0; // avoid dereferencing null node ptr
}

static size_t avl_cnt(AVLNode* node) {
    return node? node->cnt : 0;
}

static void avl_update(AVLNode* node) {
    assert(node);
    node->height = 1 + max(avl_height(node->right), avl_height(node->left));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

static AVLNode* avl_rotl(AVLNode* node) {
    AVLNode* parent = node->parent;
    AVLNode* new_node = node->right;
    AVLNode* inner = new_node->left;
    node->right = inner;

    if (inner) {
        inner->parent = node;
    }

    new_node->parent = parent; // might be null
    new_node->left = node;
    node->parent = new_node;

    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode* avl_rotr(AVLNode* node) {
    AVLNode* parent = node->parent;
    AVLNode* new_node = node->left;
    AVLNode* inner = new_node->right;
    node->left = inner;

    if (inner) {
        inner->parent = node;
    }

    new_node->parent = parent;
    new_node->right = node;
    node->parent = new_node;

    avl_update(node);
    avl_update(new_node);
    return new_node;
}


//     left-too-tall                       balanced
//           D(h+3)                           B(h+2)
//      ┌────┴────┐      rotate-right    ┌────┴────┐
//      B(h+2)    E(h)  ──────────────►  A(h+1)    D(h+1)
// ┌────┴────┐                                 ┌───┴───┐
// A(h+1)    C(h)                              C(h)    E(h)
//
//      B(h+2)                            C(h+2)
// ┌────┴────┐        rotate-left    ┌────┴────┐
// A(h)      C(h+1)  ─────────────►  B(h+1)    q(h)
//        ┌──┴──┐                 ┌──┴──┐
//        p(h)  q(h)              A(h)  p(h)

static AVLNode* avl_fix_left(AVLNode* node) {
    // we need to check the heights of the node->left children:
    // if the right's is greater, a single left rotation won't suffice.
    // We then have to rotate left the subtree with root node->left, maintaining
    // the unblanance but with a left-left case now. Hence, we can rotate right to
    // solve the unbalance

    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = avl_rotl(node->left);
    }
    
    return avl_rotr(node);
}

// mirror image of left. there's no orientation
static AVLNode* avl_fix_right(AVLNode* node) {
    if (avl_height(node->right->right) < avl_height(node->right->left)) {
        node->right = avl_rotr(node->right);
    }

    return avl_rotl(node);
}

AVLNode* avl_fix(AVLNode* node) {
    while (true) {
        AVLNode** from = &node;
        AVLNode* parent = node->parent;

        // updating from if there's a parent (so we can dereference
        // and update parent->* directly)
        if (parent) {
            from = node == parent->right? &parent->right : &parent->left;
        }

        avl_update(node);

        size_t left = avl_height(node->left);
        size_t right = avl_height(node->right);
        
        if (left == right + 2) {
            *from = avl_fix_left(node);
        } else if (right == left + 2) {
            *from = avl_fix_right(node);
        }

        if (!parent) {
            return *from;
        }

        node = parent;  
    }
}

// easy case: at most 1 child,
// hard case:
static AVLNode* avl_del_easy(AVLNode* node) {
    assert(!node->left || !node->right);
    AVLNode* child = node->left? node->left : node->right;
    AVLNode* parent = node->parent;

    if (child) {
        child->parent = parent;
    }

    if (!parent) {
        return child; // no parent, child is the new root
    }

    AVLNode** from = parent->left == node? &parent->left : &parent->right;
    *from = child; // update the parent's child 

    return avl_fix(parent); // rebalance and return the new root
}

AVLNode* avl_del(AVLNode* node) {
    if (!node->right || !node->left) {
        return avl_del_easy(node);
    }

    AVLNode* victim = node->right;
    while (victim->left) {
        victim = victim->left; // find the smallest node greater than node
    }

    AVLNode* root = avl_del_easy(victim); // detach victim from the tree
    *victim = *node; // we copy the attributes from node into victim
    
    // update the children's parent: they still hold the memory address of node,
    // which at this point has been (logically) subsituted by victim
    if (victim->left) {
        victim->left->parent = victim;
    }

    if (victim->right) {
        victim->right->parent = victim;
    }

    AVLNode** from = &root;
    AVLNode* parent = node->parent;
    if (parent) {
        from = parent->left == node? &parent->left : &parent->right;
    }
    *from = victim; 

    // if node had a parent now from points to the address of
    // parent->right or parent->left, else the node was the root
    return root;
}

void avl_insert(ZSet* zset, ZNode* znode) {
    AVLNode** from = &zset->root;
    AVLNode* parent = NULL;

    while (*from) { 
        // traverse the tree and find a slot 
        parent = *from;
        from = zless(&znode->avl_node, parent) ? &parent->left : &parent->right;
    }
    LOG_DEBUG("slot found");

    // attach the new node and update its parent
    *from = &znode->avl_node;
    znode->avl_node.parent = parent;
    assert(znode);
    LOG_DEBUG("updating the root...");
    zset->root = avl_fix(&znode->avl_node);
}

static AVLNode* successor(AVLNode* node) {
    if (node->right) {
        // find the leftmost in the right subtree, the smallest
        // node greater than our initial parameter
        for (node = node->right; node->left; node = node->left) {}
        return node;
    }

    // find the ancestor where node is the rightmost in the left
    // subtree, so where it's the greatest node smaller than the ancestor 
    while (AVLNode* parent = node->parent) {
        if (node == parent->left) {
            return parent;
        }
        node = parent;
    }

    return NULL;
}


static AVLNode* predecessor(AVLNode* node) {
    if (node->left) {
        // find the rightmost in the left subtree, the greatest
        // node smaller than our initial parameter
        for (node = node->left; node->right; node = node->right) {}
        return node;
    }

    // find the ancestor where node is the leftmost in the right
    // subtree, so where it's the smallest node greater than the ancestor 
    while (AVLNode* parent = node->parent) {
        if (node == parent->right ) {
            return parent;
        }
        node = parent;
    }

    return NULL;
}

// simplified version of avl_offset
// AVLNode* avl_offset(AVLNode* node, int64_t offset) {
//     for (size_t i {}; i < abs(offset) && node; i++) {
//         node = offset < 0? predecessor(node) : successor(node);  
//     }
    
//     return node;

// }

// optimized offset with ranks: worst case O(2log(height))
// (traverse from a leaf till the root and then to another leaf).
// The simplified version takes O(offset), inefficient with large offsets
AVLNode* avl_offset(AVLNode* node, int64_t offset) {
    int64_t pos = 0;
    while (pos != offset) {
        if (pos < offset && pos + avl_cnt(node->right) >= offset) {
            // if the size of the right subtree + the current rank
            // is greater than the offset, the target node is somewhere
            // in the right subtree 
            node = node->right; // traverse right
            pos += avl_cnt(node->left) + 1; // update position
        } else if (pos > offset && pos - avl_cnt(node->left) <= offset) {
            // similar reasoning as before
            node = node->left;
            pos -= avl_cnt(node->right) + 1;
        } else {
            AVLNode* parent = node->parent;
            if (!parent) {
                return NULL; // root, not found 
            }

            // skip all the nodes in the left subtree
            // when decrementing rank, skip the nodes in right
            // subtree when increasing rank
            if (node == parent->right) {
                pos -= avl_cnt(node->left) + 1;
            } else {
                pos += avl_cnt(node->right) + 1;
            }

            node = parent;
        }
    }

    return node;
}

uint64_t avl_rank(AVLNode* node) {
    if (!node) return 0;
    uint64_t rank = avl_cnt(node->left) + 1;
    AVLNode* parent = node->parent;
    if (!parent) {
        return rank;
    }
    
    while (parent->parent) {
        if (node == parent->right) {
            rank += avl_cnt(node->left);
            rank += avl_cnt(parent->left);
        } else if (node == parent->left) {
            rank += avl_cnt(node->right);
        }
        rank++;
        node = node->parent;
        parent = node->parent;
    }

    return rank += avl_cnt(parent->left) + 1;
}






