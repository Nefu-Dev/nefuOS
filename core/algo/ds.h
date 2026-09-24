// nefuOS data structure library
// Classic containers implemented from scratch on plain arrays / nodes —
// no STL, no heap fragmentation beyond explicit new/delete per node.
// Each structure documents its key operations and complexity; every class
// owns its memory (clear() frees all nodes) and the self test at the
// bottom exercises all operations including edge cases.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace algo {

// =====================================================================
// IntStack — dynamic array stack, O(1) push/pop/top
// =====================================================================
class IntStack {
public:
    IntStack() : data_(0), len_(0), cap_(0) {}
    ~IntStack() { clear(); }
    void push(int v);
    int  pop();                  // returns 0 on empty
    int  top() const { return len_ ? data_[len_ - 1] : 0; }
    int  size() const { return len_; }
    bool empty() const { return len_ == 0; }
    void clear();
private:
    int* data_;
    int  len_, cap_;
    void grow();
};

// =====================================================================
// IntQueue — ring buffer, O(1) push/pop
// =====================================================================
class IntQueue {
public:
    IntQueue() : data_(0), head_(0), tail_(0), cap_(0), full_(false) {}
    ~IntQueue() { clear(); }
    void push(int v);
    int  pop();                  // returns 0 on empty
    int  front() const;
    int  size() const;
    bool empty() const { return size() == 0; }
    void clear();
private:
    int* data_;
    int  head_, tail_, cap_;
    bool full_;
    void grow();
};

// =====================================================================
// IntDeque — double-ended queue on a circular buffer
// =====================================================================
class IntDeque {
public:
    IntDeque() : data_(0), head_(0), tail_(0), cap_(0) {}
    ~IntDeque() { clear(); }
    void push_front(int v);
    void push_back(int v);
    int  pop_front();
    int  pop_back();
    int  front() const;
    int  back() const;
    int  size() const;
    bool empty() const { return head_ == tail_; }
    void clear();
private:
    int* data_;
    int  head_, tail_, cap_;     // [head_, tail_) with wrap
    void grow();
};

// =====================================================================
// IntHeap — binary min-heap (or max-heap via `max` flag), O(log n) ops
// =====================================================================
class IntHeap {
public:
    IntHeap() : data_(0), len_(0), cap_(0), max_(false) {}
    ~IntHeap() { clear(); }
    void set_max(bool m) { max_ = m; }
    void push(int v);
    int  pop();                  // removes + returns the root
    int  top() const { return len_ ? data_[0] : 0; }
    int  size() const { return len_; }
    bool empty() const { return len_ == 0; }
    void clear();
private:
    int*  data_;
    int   len_, cap_;
    bool  max_;
    bool  better(int a, int b) const;   // a precedes b under the order
    void  sift_up(int i);
    void  sift_down(int i);
    void  grow();
};

// =====================================================================
// IntLinkedList — singly linked list
// =====================================================================
class IntLinkedList {
public:
    struct Node { int val; Node* next; };
    IntLinkedList() : head_(0), n_(0) {}
    ~IntLinkedList() { clear(); }
    void  push_front(int v);             // O(1)
    void  push_back(int v);              // O(n)
    bool  insert_after(Node* pos, int v); // O(1) after a found node
    bool  remove(int v);                 // first occurrence, O(n)
    Node* find(int v) const;             // O(n)
    Node* head() const { return head_; }
    int   size() const { return n_; }
    bool  empty() const { return n_ == 0; }
    void  reverse();                     // O(n) in-place
    int   kth_from_end(int k, bool* ok) const;  // one-pass classic
    bool  has_cycle() const;             // Floyd's tortoise & hare
    void  clear();
private:
    Node* head_;
    int   n_;
};

// =====================================================================
// IntDoublyList — doubly linked list with tail pointer
// =====================================================================
class IntDoublyList {
public:
    struct Node { int val; Node* prev; Node* next; };
    IntDoublyList() : head_(0), tail_(0), n_(0) {}
    ~IntDoublyList() { clear(); }
    void  push_front(int v);
    void  push_back(int v);
    int   pop_front();
    int   pop_back();
    void  remove(Node* node);            // O(1) given the node
    Node* find(int v) const;
    Node* head() const { return head_; }
    Node* tail() const { return tail_; }
    int   size() const { return n_; }
    void  clear();
private:
    Node* head_;
    Node* tail_;
    int   n_;
};

// =====================================================================
// IntBST — binary search tree (no balancing; see IntAVL)
// =====================================================================
class IntBST {
public:
    struct Node { int val; Node* l; Node* r; };
    IntBST() : root_(0), n_(0) {}
    ~IntBST() { clear(); }
    void  insert(int v);                 // iterative
    bool  remove(int v);                 // iterative, two-child successor trick
    bool  contains(int v) const;
    int   min() const;
    int   max() const;
    int   height() const;                // recursive depth
    int   count() const { return n_; }
    void  inorder(int* out, int* len) const;   // sorted walk
    void  clear();
private:
    Node* root_;
    int   n_;
    static int height_of(Node* n);
    static void inorder_of(Node* n, int* out, int* len);
    static void clear_of(Node* n);
};

// =====================================================================
// IntAVL — self-balancing BST (balance factor kept in each node)
// =====================================================================
class IntAVL {
public:
    struct Node { int val; int h; Node* l; Node* r; };
    IntAVL() : root_(0), n_(0) {}
    ~IntAVL() { clear(); }
    void  insert(int v);
    bool  remove(int v);
    bool  contains(int v) const;
    int   height() const { return root_ ? root_->h : 0; }
    int   count() const { return n_; }
    bool  is_balanced() const;           // every node |bf| <= 1
    void  inorder(int* out, int* len) const;
    void  clear();
private:
    Node* root_;
    int   n_;
    static int  h_of(Node* n);
    static int  bf_of(Node* n);
    static Node* rot_l(Node* n);
    static Node* rot_r(Node* n);
    static Node* insert_of(Node* n, int v);
    static Node* remove_of(Node* n, int v, bool* removed);
    static Node* min_of(Node* n);
    static bool  balanced_of(Node* n);
    static void  inorder_of(Node* n, int* out, int* len);
    static void  clear_of(Node* n);
};

// =====================================================================
// IntTrie — prefix tree over lowercase a-z keys
// =====================================================================
class IntTrie {
public:
    struct Node {
        Node* child[26];
        int   count;             // how many words pass through here
        bool  end;               // a word finishes at this node
        Node() : count(0), end(false) { for (int i = 0; i < 26; i++) child[i] = 0; }
    };
    IntTrie() : root_(new Node()), n_(0) {}
    ~IntTrie() { clear(); }
    void  insert(const char* s);
    bool  contains(const char* s) const;
    bool  remove(const char* s);          // returns false if absent
    int   count_prefix(const char* s) const;   // words with this prefix
    int   count() const { return n_; }
    void  clear();
private:
    Node* root_;
    int   n_;
};

// =====================================================================
// IntUnionFind — disjoint set with path compression + union by rank
// =====================================================================
class IntUnionFind {
public:
    explicit IntUnionFind(int n);
    ~IntUnionFind() { delete[] parent_; delete[] rank_; }
    int   find(int x);                    // with path compression
    bool  unite(int a, int b);            // returns true when merged
    bool  same(int a, int b) { return find(a) == find(b); }
    int   count() const;                  // number of components
    void  reset(int n);
private:
    int* parent_;
    int* rank_;
    int  n_;
    int  comps_;
};

// =====================================================================
// IntHashMap — chained hash map, power-of-two capacity, grow on load
// =====================================================================
class IntHashMap {
public:
    struct Entry { int key; int val; Entry* next; };
    IntHashMap() : buckets_(0), cap_(0), n_(0) { grow(8); }
    ~IntHashMap() { clear(); }
    void  put(int key, int val);
    bool  get(int key, int* out) const;
    bool  remove(int key);
    bool  contains(int key) const;
    int   size() const { return n_; }
    void  clear();
private:
    Entry** buckets_;
    int     cap_;
    int     n_;
    static uint32_t hash_key(int k);
    void grow(int newcap);
    void rehash(int newcap);
};

// =====================================================================
// IntSkipList — probabilistic ordered list, O(log n) expected
// =====================================================================
class IntSkipList {
public:
    struct Node { int val; Node** next; int lvl; };
    IntSkipList() : head_(0), n_(0), max_lvl_(8), rng_(0x9E3779B9) {
        head_ = make_node(0x7FFFFFFF, max_lvl_);
    }
    ~IntSkipList() { clear(); }
    void  insert(int v);
    bool  contains(int v) const;
    bool  remove(int v);
    int   size() const { return n_; }
    void  clear();
private:
    Node* head_;
    int   n_;
    int   max_lvl_;
    uint32_t rng_;
    Node* make_node(int v, int lvl) const;
    int   random_level();
    void  delete_node(Node* nd) const;
};

// =====================================================================
// self test — returns number of failed assertions
// =====================================================================
int ds_self_test();

} // namespace algo
} // namespace nefu
