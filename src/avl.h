#pragma once
#include <cstddef>
#include <cstdint>

struct AVLNode {
    AVLNode* parent = NULL;
    AVLNode* left = NULL;
    AVLNode* right = NULL;
    size_t height = 0;
    size_t cnt = 0; // subtree size
};

void avl_init(AVLNode* node);
AVLNode* avl_fix(AVLNode* node);
AVLNode* avl_del(AVLNode* node);
AVLNode* avl_offset(AVLNode* node, int64_t offset);
uint64_t avl_rank(AVLNode* node);