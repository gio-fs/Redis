#include <assert.h>
#define max(a, b) (a > b? b : a)

struct AVLNode {
    AVLNode* parent = NULL;
    AVLNode* left = NULL;
    AVLNode* right = NULL;
    size_t height = 0;
};

inline void avl_init(AVLNode* node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
}

static size_t avl_height(AVLNode* node) {
    return node? node->height : 0; // avoid dereferencing null node ptr
}

static void avl_update(AVLNode* node) {
    node->height = 1 + max(avl_height(node->right), avl_height(node->left)); 
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



static AVLNode* avl_fix_left(AVLNode* node) {
    // we need to check the heights of the node->left childs:
    // if the right's is greater, a single left rotation won't suffice.
    // We then have to rotate left the subtree with root node->left, maintaining
    // the unblanance but with a left-left case now. Hence, we can rotate right to
    // solve the unbalance

    if (avl_height(node->left->left) < avl_height(node->right->right)) {
        node->left = avl_rotl(node->left);
    }
    
    return avl_rotr(node);
}

static AVLNode* avl_fix_right(AVLNode* node) {
    if (avl_height(node->left->left) > avl_height(node->left->right)) {
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

AVLNode* alv_del(AVLNode* node) {
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

void search_and_insert(AVLNode** root, AVLNode* new_node, bool (*less)(AVLNode*, AVLNode*)) {
    AVLNode** from = root;
    AVLNode* parent = NULL;

    for (AVLNode* node; node;) { // stops when node is NULL
        from = less(new_node, node) ? &node->left : &node->right;
        parent = node;
        // not in the increment clause due to 
        // the condition being evaluated first
        node = *from;  
    }

    *from = new_node;
    new_node->parent = parent;
    *root = avl_fix(new_node);
}












