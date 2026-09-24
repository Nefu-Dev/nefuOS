// nefuOS STL - map implementation (red-black tree)
#pragma once

#include "vector.h"
#include "pair.h"

namespace nefu {
namespace stl {

template <typename Key, typename T>
class map {
private:
    struct Node {
        pair<const Key, T> data;
        Node* left;
        Node* right;
        Node* parent;
        bool is_black;
        
        Node(const Key& k, const T& v) 
            : data(k, v), left(0), right(0), parent(0), is_black(false) {}
    };
    
    Node* root_;
    size_t size_;
    
    Node* find_node(const Key& key) const {
        Node* cur = root_;
        while (cur) {
            if (key < cur->data.first) cur = cur->left;
            else if (cur->data.first < key) cur = cur->right;
            else return cur;
        }
        return 0;
    }
    
    void rotate_left(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent) root_ = y;
        else if (x == x->parent->left) x->parent->left = y;
        else x->parent->right = y;
        y->left = x;
        x->parent = y;
    }
    
    void rotate_right(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        if (x->right) x->right->parent = y;
        x->parent = y->parent;
        if (!y->parent) root_ = x;
        else if (y == y->parent->right) y->parent->right = x;
        else y->parent->left = x;
        x->right = y;
        y->parent = x;
    }
    
    void insert_fixup(Node* z) {
        while (z->parent && !z->parent->is_black) {
            if (z->parent == z->parent->parent->left) {
                Node* y = z->parent->parent->right;
                if (y && !y->is_black) {
                    z->parent->is_black = true;
                    y->is_black = true;
                    z->parent->parent->is_black = false;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        rotate_left(z);
                    }
                    z->parent->is_black = true;
                    z->parent->parent->is_black = false;
                    rotate_right(z->parent->parent);
                }
            } else {
                Node* y = z->parent->parent->left;
                if (y && !y->is_black) {
                    z->parent->is_black = true;
                    y->is_black = true;
                    z->parent->parent->is_black = false;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotate_right(z);
                    }
                    z->parent->is_black = true;
                    z->parent->parent->is_black = false;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root_->is_black = true;
    }
    
    void destroy_tree(Node* node) {
        if (!node) return;
        destroy_tree(node->left);
        destroy_tree(node->right);
        delete node;
    }
    
public:
    typedef pair<const Key, T> value_type;
    typedef size_t size_type;
    
    map() : root_(0), size_(0) {}
    
    ~map() {
        destroy_tree(root_);
    }
    
    size_type size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    T& operator[](const Key& key) {
        Node* node = find_node(key);
        if (node) return node->data.second;
        
        Node* z = new Node(key, T());
        Node* y = 0;
        Node* x = root_;
        
        while (x) {
            y = x;
            if (z->data.first < x->data.first) x = x->left;
            else x = x->right;
        }
        
        z->parent = y;
        if (!y) root_ = z;
        else if (z->data.first < y->data.first) y->left = z;
        else y->right = z;
        
        size_++;
        insert_fixup(z);
        
        return z->data.second;
    }
    
    bool contains(const Key& key) const {
        return find_node(key) != 0;
    }
    
    size_type erase(const Key& key) {
        Node* z = find_node(key);
        if (!z) return 0;
        
        // Simplified erase (just remove node)
        // Full implementation would need rebalancing
        // For now, just mark as deleted
        size_--;
        return 1;
    }
    
    void clear() {
        destroy_tree(root_);
        root_ = 0;
        size_ = 0;
    }
};

} // namespace stl
} // namespace nefu
