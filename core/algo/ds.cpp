// nefuOS data structure library — implementation & self test
// See ds.h for the API contract.
#include "ds.h"
#include <string.h>

namespace nefu {
namespace algo {

// =====================================================================
// IntStack
// =====================================================================
void IntStack::grow() {
    int nc = cap_ ? cap_ * 2 : 8;
    int* nd = new int[(size_t)nc];
    if (data_) {
        for (int i = 0; i < len_; i++) nd[i] = data_[i];
        delete[] data_;
    }
    data_ = nd;
    cap_ = nc;
}

void IntStack::push(int v) {
    if (len_ >= cap_) grow();
    data_[len_++] = v;
}

int IntStack::pop() {
    if (!len_) return 0;
    return data_[--len_];
}

void IntStack::clear() {
    delete[] data_;
    data_ = 0;
    len_ = cap_ = 0;
}

// =====================================================================
// IntQueue (ring buffer; size() handles wrap-around)
// =====================================================================
void IntQueue::grow() {
    int nc = cap_ ? cap_ * 2 : 8;
    int* nd = new int[(size_t)nc];
    int s = 0;
    if (cap_) {
        s = size();
        for (int i = 0; i < s; i++) nd[i] = data_[(head_ + i) % cap_];
    }
    delete[] data_;
    data_ = nd;
    head_ = 0;
    tail_ = s;
    cap_ = nc;
    full_ = false;
}

void IntQueue::push(int v) {
    if (full_ || cap_ == 0) grow();
    data_[tail_] = v;
    tail_ = (tail_ + 1) % cap_;
    if (tail_ == head_) full_ = true;
}

int IntQueue::pop() {
    if (empty()) return 0;
    int v = data_[head_];
    head_ = (head_ + 1) % cap_;
    full_ = false;
    return v;
}

int IntQueue::front() const {
    return empty() ? 0 : data_[head_];
}

int IntQueue::size() const {
    if (full_) return cap_;
    return (tail_ - head_ + cap_) % cap_;
}

void IntQueue::clear() {
    delete[] data_;
    data_ = 0;
    head_ = tail_ = cap_ = 0;
    full_ = false;
}

// =====================================================================
// IntDeque
// =====================================================================
void IntDeque::grow() {
    int nc = cap_ ? cap_ * 2 : 8;
    int* nd = new int[(size_t)nc];
    int s = 0;
    if (cap_) {
        s = size();
        for (int i = 0; i < s; i++) nd[i] = data_[(head_ + i) % cap_];
    }
    delete[] data_;
    data_ = nd;
    head_ = 0;
    tail_ = s;
    cap_ = nc;
}

void IntDeque::push_front(int v) {
    if (cap_ == 0 || size() >= cap_) grow();
    head_ = (head_ - 1 + cap_) % cap_;
    data_[head_] = v;
}

void IntDeque::push_back(int v) {
    if (cap_ == 0 || size() >= cap_) grow();
    data_[tail_] = v;
    tail_ = (tail_ + 1) % cap_;
}

int IntDeque::pop_front() {
    if (empty()) return 0;
    int v = data_[head_];
    head_ = (head_ + 1) % cap_;
    return v;
}

int IntDeque::pop_back() {
    if (empty()) return 0;
    tail_ = (tail_ - 1 + cap_) % cap_;
    return data_[tail_];
}

int IntDeque::front() const { return empty() ? 0 : data_[head_]; }
int IntDeque::back() const  { return empty() ? 0 : data_[(tail_ - 1 + cap_) % cap_]; }
int IntDeque::size() const  { return (tail_ - head_ + cap_) % cap_; }

void IntDeque::clear() {
    delete[] data_;
    data_ = 0;
    head_ = tail_ = cap_ = 0;
}

// =====================================================================
// IntHeap
// =====================================================================
bool IntHeap::better(int a, int b) const {
    return max_ ? a > b : a < b;
}

void IntHeap::grow() {
    int nc = cap_ ? cap_ * 2 : 8;
    int* nd = new int[(size_t)nc];
    if (data_) {
        for (int i = 0; i < len_; i++) nd[i] = data_[i];
        delete[] data_;
    }
    data_ = nd;
    cap_ = nc;
}

void IntHeap::sift_up(int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (better(data_[i], data_[p])) {
            int t = data_[i]; data_[i] = data_[p]; data_[p] = t;
            i = p;
        } else break;
    }
}

void IntHeap::sift_down(int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < len_ && better(data_[l], data_[m])) m = l;
        if (r < len_ && better(data_[r], data_[m])) m = r;
        if (m == i) break;
        int t = data_[i]; data_[i] = data_[m]; data_[m] = t;
        i = m;
    }
}

void IntHeap::push(int v) {
    if (len_ >= cap_) grow();
    data_[len_++] = v;
    sift_up(len_ - 1);
}

int IntHeap::pop() {
    if (!len_) return 0;
    int v = data_[0];
    data_[0] = data_[--len_];
    if (len_) sift_down(0);
    return v;
}

void IntHeap::clear() {
    delete[] data_;
    data_ = 0;
    len_ = cap_ = 0;
}

// =====================================================================
// IntLinkedList
// =====================================================================
void IntLinkedList::push_front(int v) {
    Node* nd = new Node();
    nd->val = v;
    nd->next = head_;
    head_ = nd;
    n_++;
}

void IntLinkedList::push_back(int v) {
    Node* nd = new Node();
    nd->val = v;
    nd->next = 0;
    if (!head_) { head_ = nd; }
    else {
        Node* p = head_;
        while (p->next) p = p->next;
        p->next = nd;
    }
    n_++;
}

bool IntLinkedList::insert_after(Node* pos, int v) {
    if (!pos) return false;
    Node* nd = new Node();
    nd->val = v;
    nd->next = pos->next;
    pos->next = nd;
    n_++;
    return true;
}

bool IntLinkedList::remove(int v) {
    Node* prev = 0;
    Node* cur = head_;
    while (cur && cur->val != v) { prev = cur; cur = cur->next; }
    if (!cur) return false;
    if (prev) prev->next = cur->next;
    else      head_ = cur->next;
    delete cur;
    n_--;
    return true;
}

IntLinkedList::Node* IntLinkedList::find(int v) const {
    Node* p = head_;
    while (p && p->val != v) p = p->next;
    return p;
}

void IntLinkedList::reverse() {
    Node* prev = 0;
    Node* cur = head_;
    while (cur) {
        Node* next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    head_ = prev;
}

int IntLinkedList::kth_from_end(int k, bool* ok) const {
    // two-pointer: advance `fast` k steps, then walk both together
    Node* fast = head_;
    for (int i = 0; i < k; i++) {
        if (!fast) { if (ok) *ok = false; return 0; }
        fast = fast->next;
    }
    Node* slow = head_;
    while (fast) { slow = slow->next; fast = fast->next; }
    if (ok) *ok = true;
    return slow ? slow->val : 0;
}

bool IntLinkedList::has_cycle() const {
    Node* slow = head_;
    Node* fast = head_;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return true;
    }
    return false;
}

void IntLinkedList::clear() {
    Node* p = head_;
    while (p) { Node* nx = p->next; delete p; p = nx; }
    head_ = 0;
    n_ = 0;
}

// =====================================================================
// IntDoublyList
// =====================================================================
void IntDoublyList::push_front(int v) {
    Node* nd = new Node();
    nd->val = v;
    nd->prev = 0;
    nd->next = head_;
    if (head_) head_->prev = nd;
    else tail_ = nd;
    head_ = nd;
    n_++;
}

void IntDoublyList::push_back(int v) {
    Node* nd = new Node();
    nd->val = v;
    nd->prev = tail_;
    nd->next = 0;
    if (tail_) tail_->next = nd;
    else head_ = nd;
    tail_ = nd;
    n_++;
}

int IntDoublyList::pop_front() {
    if (!head_) return 0;
    int v = head_->val;
    Node* nx = head_->next;
    delete head_;
    head_ = nx;
    if (head_) head_->prev = 0;
    else tail_ = 0;
    n_--;
    return v;
}

int IntDoublyList::pop_back() {
    if (!tail_) return 0;
    int v = tail_->val;
    Node* pv = tail_->prev;
    delete tail_;
    tail_ = pv;
    if (tail_) tail_->next = 0;
    else head_ = 0;
    n_--;
    return v;
}

void IntDoublyList::remove(Node* node) {
    if (!node) return;
    if (node->prev) node->prev->next = node->next;
    else head_ = node->next;
    if (node->next) node->next->prev = node->prev;
    else tail_ = node->prev;
    delete node;
    n_--;
}

IntDoublyList::Node* IntDoublyList::find(int v) const {
    Node* p = head_;
    while (p && p->val != v) p = p->next;
    return p;
}

void IntDoublyList::clear() {
    Node* p = head_;
    while (p) { Node* nx = p->next; delete p; p = nx; }
    head_ = tail_ = 0;
    n_ = 0;
}

// =====================================================================
// IntBST
// =====================================================================
void IntBST::insert(int v) {
    Node* nd = new Node();
    nd->val = v;
    nd->l = nd->r = 0;
    if (!root_) { root_ = nd; n_ = 1; return; }
    Node* cur = root_;
    for (;;) {
        if (v < cur->val) {
            if (cur->l) cur = cur->l;
            else { cur->l = nd; break; }
        } else {
            if (cur->r) cur = cur->r;
            else { cur->r = nd; break; }
        }
    }
    n_++;
}

bool IntBST::remove(int v) {
    // find the node and its parent
    Node* par = 0;
    Node* cur = root_;
    while (cur && cur->val != v) {
        par = cur;
        cur = (v < cur->val) ? cur->l : cur->r;
    }
    if (!cur) return false;
    Node** link = par ? (par->l == cur ? &par->l : &par->r) : &root_;
    if (cur->l && cur->r) {
        // two children: replace with the in-order successor (leftmost of right)
        Node* sp = cur;
        Node* s = cur->r;
        while (s->l) { sp = s; s = s->l; }
        cur->val = s->val;
        if (sp == cur) sp->r = s->r;
        else sp->l = s->r;
        delete s;
    } else {
        Node* child = cur->l ? cur->l : cur->r;
        *link = child;
        delete cur;
    }
    n_--;
    return true;
}

bool IntBST::contains(int v) const {
    Node* cur = root_;
    while (cur) {
        if (v == cur->val) return true;
        cur = (v < cur->val) ? cur->l : cur->r;
    }
    return false;
}

int IntBST::min() const {
    Node* cur = root_;
    if (!cur) return 0;
    while (cur->l) cur = cur->l;
    return cur->val;
}

int IntBST::max() const {
    Node* cur = root_;
    if (!cur) return 0;
    while (cur->r) cur = cur->r;
    return cur->val;
}

int IntBST::height() const { return height_of(root_); }
int IntBST::height_of(Node* n) {
    if (!n) return 0;
    int l = height_of(n->l), r = height_of(n->r);
    return 1 + (l > r ? l : r);
}

void IntBST::inorder(int* out, int* len) const {
    *len = 0;
    inorder_of(root_, out, len);
}

void IntBST::inorder_of(Node* n, int* out, int* len) {
    if (!n) return;
    inorder_of(n->l, out, len);
    out[(*len)++] = n->val;
    inorder_of(n->r, out, len);
}

void IntBST::clear_of(Node* n) {
    if (!n) return;
    clear_of(n->l);
    clear_of(n->r);
    delete n;
}

void IntBST::clear() {
    clear_of(root_);
    root_ = 0;
    n_ = 0;
}

// =====================================================================
// IntAVL
// =====================================================================
int IntAVL::h_of(Node* n) { return n ? n->h : 0; }
int IntAVL::bf_of(Node* n) { return h_of(n->l) - h_of(n->r); }

IntAVL::Node* IntAVL::rot_l(Node* n) {
    Node* r = n->r;
    n->r = r->l;
    r->l = n;
    n->h = 1 + (h_of(n->l) > h_of(n->r) ? h_of(n->l) : h_of(n->r));
    r->h = 1 + (h_of(r->l) > h_of(r->r) ? h_of(r->l) : h_of(r->r));
    return r;
}

IntAVL::Node* IntAVL::rot_r(Node* n) {
    Node* l = n->l;
    n->l = l->r;
    l->r = n;
    n->h = 1 + (h_of(n->l) > h_of(n->r) ? h_of(n->l) : h_of(n->r));
    l->h = 1 + (h_of(l->l) > h_of(l->r) ? h_of(l->l) : h_of(l->r));
    return l;
}

IntAVL::Node* IntAVL::insert_of(Node* n, int v) {
    if (!n) {
        Node* nd = new Node();
        nd->val = v;
        nd->h = 1;
        nd->l = nd->r = 0;
        return nd;
    }
    if (v < n->val) n->l = insert_of(n->l, v);
    else            n->r = insert_of(n->r, v);
    n->h = 1 + (h_of(n->l) > h_of(n->r) ? h_of(n->l) : h_of(n->r));
    int bf = bf_of(n);
    if (bf > 1) {
        if (v < n->l->val) return rot_r(n);              // LL
        n->l = rot_l(n->l);                              // LR
        return rot_r(n);
    }
    if (bf < -1) {
        if (v > n->r->val) return rot_l(n);              // RR
        n->r = rot_r(n->r);                              // RL
        return rot_l(n);
    }
    return n;
}

IntAVL::Node* IntAVL::remove_of(Node* n, int v, bool* removed) {
    if (!n) return 0;
    if (v < n->val)      n->l = remove_of(n->l, v, removed);
    else if (v > n->val) n->r = remove_of(n->r, v, removed);
    else {
        *removed = true;
        if (!n->l) { Node* r = n->r; delete n; return r; }
        if (!n->r) { Node* l = n->l; delete n; return l; }
        Node* succ = min_of(n->r);
        n->val = succ->val;
        n->r = remove_of(n->r, succ->val, removed);
    }
    n->h = 1 + (h_of(n->l) > h_of(n->r) ? h_of(n->l) : h_of(n->r));
    int bf = bf_of(n);
    if (bf > 1) {
        if (bf_of(n->l) < 0) n->l = rot_l(n->l);
        return rot_r(n);
    }
    if (bf < -1) {
        if (bf_of(n->r) > 0) n->r = rot_r(n->r);
        return rot_l(n);
    }
    return n;
}

IntAVL::Node* IntAVL::min_of(Node* n) {
    while (n->l) n = n->l;
    return n;
}

void IntAVL::insert(int v) {
    root_ = insert_of(root_, v);
    n_++;
}

bool IntAVL::remove(int v) {
    bool removed = false;
    root_ = remove_of(root_, v, &removed);
    if (removed) n_--;
    return removed;
}

bool IntAVL::contains(int v) const {
    Node* cur = root_;
    while (cur) {
        if (v == cur->val) return true;
        cur = (v < cur->val) ? cur->l : cur->r;
    }
    return false;
}

bool IntAVL::is_balanced() const { return balanced_of(root_); }

bool IntAVL::balanced_of(Node* n) {
    if (!n) return true;
    int bf = bf_of(n);
    if (bf > 1 || bf < -1) return false;
    return balanced_of(n->l) && balanced_of(n->r);
}

void IntAVL::inorder(int* out, int* len) const {
    *len = 0;
    inorder_of(root_, out, len);
}

void IntAVL::inorder_of(Node* n, int* out, int* len) {
    if (!n) return;
    inorder_of(n->l, out, len);
    out[(*len)++] = n->val;
    inorder_of(n->r, out, len);
}

void IntAVL::clear_of(Node* n) {
    if (!n) return;
    clear_of(n->l);
    clear_of(n->r);
    delete n;
}

void IntAVL::clear() {
    clear_of(root_);
    root_ = 0;
    n_ = 0;
}

// =====================================================================
// IntTrie
// =====================================================================
void IntTrie::insert(const char* s) {
    Node* cur = root_;
    cur->count++;
    for (; *s; s++) {
        int c = *s - 'a';
        if (c < 0 || c >= 26) return;
        if (!cur->child[c]) cur->child[c] = new Node();
        cur = cur->child[c];
        cur->count++;
    }
    cur->end = true;
    n_++;
}

bool IntTrie::contains(const char* s) const {
    Node* cur = root_;
    for (; *s; s++) {
        int c = *s - 'a';
        if (c < 0 || c >= 26) return false;
        if (!cur->child[c]) return false;
        cur = cur->child[c];
    }
    return cur->end;
}

bool IntTrie::remove(const char* s) {
    if (!contains(s)) return false;
    Node* cur = root_;
    cur->count--;
    for (; *s; s++) {
        int c = *s - 'a';
        cur = cur->child[c];
        cur->count--;
    }
    cur->end = false;
    n_--;
    return true;
}

int IntTrie::count_prefix(const char* s) const {
    Node* cur = root_;
    for (; *s; s++) {
        int c = *s - 'a';
        if (c < 0 || c >= 26) return 0;
        if (!cur->child[c]) return 0;
        cur = cur->child[c];
    }
    return cur->count;
}

void IntTrie::clear() {
    // recursive delete; the root is recreated to keep the object usable
    // (simple iterative post-order via a manual stack would be longer;
    // recursion depth is bounded by word length in practice)
    struct Free { static void walk(Node* n) {
        if (!n) return;
        for (int i = 0; i < 26; i++) walk(n->child[i]);
        delete n;
    }};
    Free::walk(root_);
    root_ = new Node();
    n_ = 0;
}

// =====================================================================
// IntUnionFind
// =====================================================================
IntUnionFind::IntUnionFind(int n) : n_(n), comps_(n) {
    parent_ = new int[(size_t)n];
    rank_ = new int[(size_t)n];
    for (int i = 0; i < n; i++) { parent_[i] = i; rank_[i] = 0; }
}

int IntUnionFind::find(int x) {
    if (parent_[x] != x) parent_[x] = find(parent_[x]);  // path compression
    return parent_[x];
}

bool IntUnionFind::unite(int a, int b) {
    a = find(a); b = find(b);
    if (a == b) return false;
    // union by rank: attach the shorter tree under the taller one
    if (rank_[a] < rank_[b]) { parent_[a] = b; }
    else if (rank_[a] > rank_[b]) { parent_[b] = a; }
    else { parent_[b] = a; rank_[a]++; }
    comps_--;
    return true;
}

int IntUnionFind::count() const { return comps_; }

void IntUnionFind::reset(int n) {
    delete[] parent_;
    delete[] rank_;
    n_ = n;
    comps_ = n;
    parent_ = new int[(size_t)n];
    rank_ = new int[(size_t)n];
    for (int i = 0; i < n; i++) { parent_[i] = i; rank_[i] = 0; }
}

// =====================================================================
// IntHashMap
// =====================================================================
uint32_t IntHashMap::hash_key(int k) {
    // Thomas Wang integer hash — good avalanche for power-of-two tables
    uint32_t h = (uint32_t)k;
    h = (h ^ 61) ^ (h >> 16);
    h = h + (h << 3);
    h = h ^ (h >> 4);
    h = h * 0x27D4EB2D;
    h = h ^ (h >> 15);
    return h;
}

void IntHashMap::grow(int newcap) {
    int oldcap = cap_;
    Entry** old = buckets_;
    cap_ = newcap;
    buckets_ = new Entry*[(size_t)newcap];
    for (int i = 0; i < newcap; i++) buckets_[i] = 0;
    n_ = 0;
    if (old) {
        for (int i = 0; i < oldcap; i++) {
            Entry* e = old[i];
            while (e) {
                Entry* nx = e->next;
                put(e->key, e->val);
                delete e;
                e = nx;
            }
        }
        delete[] old;
    }
}

void IntHashMap::rehash(int newcap) { grow(newcap); }

void IntHashMap::put(int key, int val) {
    uint32_t h = hash_key(key) & (uint32_t)(cap_ - 1);
    Entry* e = buckets_[h];
    while (e) {
        if (e->key == key) { e->val = val; return; }
        e = e->next;
    }
    Entry* nd = new Entry();
    nd->key = key;
    nd->val = val;
    nd->next = buckets_[h];
    buckets_[h] = nd;
    n_++;
    // grow when the load factor exceeds 0.75
    if (n_ * 4 > cap_ * 3) grow(cap_ * 2);
}

bool IntHashMap::get(int key, int* out) const {
    uint32_t h = hash_key(key) & (uint32_t)(cap_ - 1);
    Entry* e = buckets_[h];
    while (e) {
        if (e->key == key) { if (out) *out = e->val; return true; }
        e = e->next;
    }
    return false;
}

bool IntHashMap::remove(int key) {
    uint32_t h = hash_key(key) & (uint32_t)(cap_ - 1);
    Entry* prev = 0;
    Entry* e = buckets_[h];
    while (e) {
        if (e->key == key) {
            if (prev) prev->next = e->next;
            else buckets_[h] = e->next;
            delete e;
            n_--;
            return true;
        }
        prev = e;
        e = e->next;
    }
    return false;
}

bool IntHashMap::contains(int key) const {
    uint32_t h = hash_key(key) & (uint32_t)(cap_ - 1);
    Entry* e = buckets_[h];
    while (e) {
        if (e->key == key) return true;
        e = e->next;
    }
    return false;
}

void IntHashMap::clear() {
    for (int i = 0; i < cap_; i++) {
        Entry* e = buckets_[i];
        while (e) { Entry* nx = e->next; delete e; e = nx; }
        buckets_[i] = 0;
    }
    n_ = 0;
}

// =====================================================================
// IntSkipList
// =====================================================================
IntSkipList::Node* IntSkipList::make_node(int v, int lvl) const {
    Node* nd = new Node();
    nd->val = v;
    nd->lvl = lvl;
    nd->next = new Node*[(size_t)lvl];
    for (int i = 0; i < lvl; i++) nd->next[i] = 0;
    return nd;
}

void IntSkipList::delete_node(Node* nd) const {
    if (!nd) return;
    delete[] nd->next;
    delete nd;
}

int IntSkipList::random_level() {
    // geometric distribution: keep halving until ~1/2 stops
    int lvl = 1;
    while ((rng_ = rng_ * 1664525u + 1013904223u) % 2 == 0 && lvl < max_lvl_) lvl++;
    return lvl;
}

void IntSkipList::insert(int v) {
    Node* update[16];
    Node* cur = head_;
    for (int i = max_lvl_ - 1; i >= 0; i--) {
        while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
        update[i] = cur;
    }
    int lvl = random_level();
    Node* nd = make_node(v, lvl);
    for (int i = 0; i < lvl; i++) {
        nd->next[i] = update[i]->next[i];
        update[i]->next[i] = nd;
    }
    n_++;
}

bool IntSkipList::contains(int v) const {
    Node* cur = head_;
    for (int i = max_lvl_ - 1; i >= 0; i--)
        while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
    Node* fwd = cur->next[0];
    return fwd && fwd->val == v;
}

bool IntSkipList::remove(int v) {
    Node* update[16];
    Node* cur = head_;
    for (int i = max_lvl_ - 1; i >= 0; i--) {
        while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
        update[i] = cur;
    }
    Node* target = cur->next[0];
    if (!target || target->val != v) return false;
    for (int i = 0; i < target->lvl; i++)
        if (update[i]->next[i] == target) update[i]->next[i] = target->next[i];
    delete_node(target);
    n_--;
    return true;
}

void IntSkipList::clear() {
    Node* cur = head_->next[0];
    while (cur) { Node* nx = cur->next[0]; delete_node(cur); cur = nx; }
    for (int i = 0; i < max_lvl_; i++) head_->next[i] = 0;
    n_ = 0;
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_ds_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_ds_fails++;
    (void)what;
}
} // namespace

int ds_self_test() {
    g_ds_fails = 0;

    // ---- stack ----
    {
        IntStack s;
        expect("stack-empty", s.empty());
        for (int i = 0; i < 100; i++) s.push(i);
        expect("stack-size", s.size() == 100);
        expect("stack-top", s.top() == 99);
        int v = s.pop();
        expect("stack-pop", v == 99);
        expect("stack-size2", s.size() == 99);
        s.clear();
        expect("stack-clear", s.empty());
    }
    // ---- queue ----
    {
        IntQueue q;
        for (int i = 0; i < 50; i++) q.push(i);
        expect("queue-front", q.front() == 0);
        expect("queue-size", q.size() == 50);
        for (int i = 0; i < 25; i++) q.pop();
        expect("queue-pop", q.front() == 25);
        expect("queue-size2", q.size() == 25);
        for (int i = 50; i < 100; i++) q.push(i);   // wrap around
        expect("queue-wrap", q.size() == 75);
        expect("queue-front2", q.front() == 25);
        q.pop();
        expect("queue-front3", q.front() == 26);
    }
    // ---- deque ----
    {
        IntDeque d;
        d.push_back(1);
        d.push_front(0);
        d.push_back(2);
        expect("deque-front", d.front() == 0);
        expect("deque-back", d.back() == 2);
        expect("deque-popb", d.pop_back() == 2);
        expect("deque-popfr", d.pop_front() == 0);
        expect("deque-size", d.size() == 1);
        d.push_front(9);
        expect("deque-front2", d.front() == 9);
    }
    // ---- heap (min + max) ----
    {
        IntHeap h;
        h.push(5); h.push(1); h.push(8); h.push(3); h.push(2);
        expect("heap-min", h.top() == 1);
        expect("heap-pop", h.pop() == 1);
        expect("heap-pop2", h.pop() == 2);
        expect("heap-pop3", h.pop() == 3);
        expect("heap-size", h.size() == 2);
        IntHeap mh;
        mh.set_max(true);
        mh.push(5); mh.push(1); mh.push(8);
        expect("heap-max", mh.top() == 8);
        expect("heap-maxpop", mh.pop() == 8);
    }
    // ---- singly linked list ----
    {
        IntLinkedList l;
        l.push_back(1);
        l.push_back(2);
        l.push_back(3);
        l.push_front(0);
        expect("list-size", l.size() == 4);
        expect("list-find", l.find(2) != 0);
        expect("list-find-miss", l.find(9) == 0);
        bool ok = false;
        expect("list-kth", l.kth_from_end(1, &ok) == 3 && ok);
        expect("list-kth-out", l.kth_from_end(9, &ok) == 0 && !ok);
        l.reverse();
        expect("list-rev", l.kth_from_end(1, &ok) == 0 && ok);
        expect("list-remove", l.remove(2));
        expect("list-remove-miss", !l.remove(42));
        IntLinkedList cyc;
        cyc.push_back(1);
        cyc.push_back(2);
        expect("list-nocycle", !cyc.has_cycle());
        IntLinkedList::Node* p1 = cyc.find(1);
        IntLinkedList::Node* p2 = cyc.find(2);
        if (p1 && p2) p2->next = p1;         // craft a cycle
        expect("list-cycle", cyc.has_cycle());
        p2->next = 0;                        // break it before clear
    }
    // ---- doubly linked list ----
    {
        IntDoublyList d;
        d.push_back(10);
        d.push_back(20);
        d.push_front(5);
        expect("dlist-size", d.size() == 3);
        expect("dlist-front", d.pop_front() == 5);
        expect("dlist-back", d.pop_back() == 20);
        expect("dlist-mid", d.pop_front() == 10);
        expect("dlist-empty", d.size() == 0);
        d.push_back(7);
        d.push_back(8);
        IntDoublyList::Node* n7 = d.find(7);
        d.remove(n7);
        expect("dlist-remove", d.size() == 1 && d.head() && d.head()->val == 8);
    }
    // ---- BST ----
    {
        IntBST t;
        int ins[] = {50, 30, 70, 20, 40, 60, 80, 35};
        for (int i = 0; i < 8; i++) t.insert(ins[i]);
        expect("bst-count", t.count() == 8);
        expect("bst-contains", t.contains(35) && t.contains(80));
        expect("bst-miss", !t.contains(99));
        expect("bst-min", t.min() == 20);
        expect("bst-max", t.max() == 80);
        int order[16], len = 0;
        t.inorder(order, &len);
        bool sorted = true;
        for (int i = 1; i < len; i++) if (order[i - 1] >= order[i]) sorted = false;
        expect("bst-inorder", sorted && len == 8);
        expect("bst-remove-leaf", t.remove(35));
        expect("bst-remove-2child", t.remove(50));
        expect("bst-remove-miss", !t.remove(50));
        expect("bst-count2", t.count() == 6);
        expect("bst-height", t.height() >= 1);
    }
    // ---- AVL ----
    {
        IntAVL av;
        // insert an ascending sequence: would be a degenerate chain in a BST
        for (int i = 1; i <= 100; i++) av.insert(i);
        expect("avl-count", av.count() == 100);
        expect("avl-balanced", av.is_balanced());
        expect("avl-height-log", av.height() <= 8);   // log2(100) ~= 7
        expect("avl-contains", av.contains(100) && av.contains(1));
        expect("avl-miss", !av.contains(0));
        // remove evens and recheck balance
        for (int i = 2; i <= 100; i += 2) expect("avl-rm", av.remove(i));
        expect("avl-count2", av.count() == 50);
        expect("avl-balanced2", av.is_balanced());
        int order[128], len = 0;
        av.inorder(order, &len);
        bool sorted = true;
        for (int i = 1; i < len; i++) if (order[i - 1] >= order[i]) sorted = false;
        expect("avl-inorder", sorted && len == 50);
        expect("avl-rm-miss", !av.remove(12345));
    }
    // ---- trie ----
    {
        IntTrie tr;
        tr.insert("apple");
        tr.insert("app");
        tr.insert("apply");
        tr.insert("banana");
        expect("trie-count", tr.count() == 4);
        expect("trie-contains", tr.contains("apple") && tr.contains("app"));
        expect("trie-miss", !tr.contains("appl"));
        expect("trie-prefix", tr.count_prefix("app") == 3);
        expect("trie-prefix2", tr.count_prefix("b") == 1);
        expect("trie-rm", tr.remove("app"));
        expect("trie-rm2", tr.count() == 3 && !tr.contains("app"));
        expect("trie-prefix3", tr.count_prefix("app") == 2);
        expect("trie-rm-miss", !tr.remove("zzz"));
    }
    // ---- union-find ----
    {
        IntUnionFind uf(10);
        expect("uf-init", uf.count() == 10);
        uf.unite(0, 1);
        uf.unite(1, 2);
        uf.unite(3, 4);
        expect("uf-same", uf.same(0, 2));
        expect("uf-diff", !uf.same(0, 3));
        expect("uf-count", uf.count() == 7);
        uf.unite(2, 3);
        expect("uf-chain", uf.same(0, 4));
        expect("uf-count2", uf.count() == 6);
    }
    // ---- hash map ----
    {
        IntHashMap hm;
        for (int i = 0; i < 1000; i++) hm.put(i, i * 2);
        expect("hm-size", hm.size() == 1000);
        int out = -1;
        expect("hm-get", hm.get(777, &out) && out == 1554);
        expect("hm-contains", hm.contains(999));
        expect("hm-miss", !hm.contains(-1));
        expect("hm-rm", hm.remove(500));
        expect("hm-rm2", !hm.get(500, &out));
        expect("hm-size2", hm.size() == 999);
        hm.put(7, 100);                      // overwrite
        expect("hm-overwrite", hm.get(7, &out) && out == 100);
        expect("hm-rm-miss", !hm.remove(123456));
    }
    // ---- skip list ----
    {
        IntSkipList sl;
        for (int i = 0; i < 200; i++) sl.insert(i * 3);
        expect("sl-size", sl.size() == 200);
        expect("sl-contains", sl.contains(0) && sl.contains(597));
        expect("sl-miss", !sl.contains(1) && !sl.contains(598));
        expect("sl-rm", sl.remove(300));
        expect("sl-rm2", !sl.contains(300) && sl.size() == 199);
        expect("sl-rm-miss", !sl.remove(301));
    }
    return g_ds_fails;
}

} // namespace algo
} // namespace nefu
