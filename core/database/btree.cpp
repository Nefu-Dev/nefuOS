// nefuOS 嵌入式数据库 —— B+ 树实现
#include "btree.h"
#include "../klib/klib.h"

namespace nefu {
namespace database {

// ===================== 键比较 =====================
// 二进制安全的字典序:先按短的公共前缀 memcmp,前缀相同则短键更小。
static int bkey_cmp(const String& a, const String& b) {
    int la = a.len(), lb = b.len();
    int m = la < lb ? la : lb;
    if (m > 0) {
        int c = memcmp(a.c_str(), b.c_str(), (size_t)m);
        if (c != 0) return c;
    }
    return la - lb;
}

// ===================== 节点 =====================
BNode::BNode(bool leaf) : is_leaf(leaf), n(0), next(0) {
    for (int i = 0; i <= BTREE_ORDER; i++) child[i] = 0;
}

BNode::~BNode() {
    if (!is_leaf) {
        for (int i = 0; i <= BTREE_ORDER; i++) {
            if (child[i]) { delete child[i]; child[i] = 0; }
        }
    }
}

// 在叶子节点里线性找 key 的下标,找不到返回 -1
int BPlusTree::leaf_find(BNode* leaf, const String& key) {
    for (int i = 0; i < leaf->n; i++) {
        int c = bkey_cmp(leaf->keys[i], key);
        if (c == 0) return i;
        if (c > 0) break;     // 已超过,后面更大
    }
    return -1;
}

// ===================== 构造 / 析构 / 清空 =====================
BPlusTree::BPlusTree() : root_(0), count_(0) {}

BPlusTree::~BPlusTree() { clear(); }

void BPlusTree::clear() {
    if (root_) delete root_;
    root_ = 0;
    count_ = 0;
}

// ===================== 查找 =====================
BNode* BPlusTree::leftmost_leaf() const {
    BNode* n = root_;
    if (!n) return 0;
    while (!n->is_leaf) n = n->child[0];
    return n;
}

BNode* BPlusTree::seek_leaf(const String& key, int& slot_out) const {
    BNode* n = root_;
    if (!n) { slot_out = 0; return 0; }
    while (!n->is_leaf) {
        int i = 0;
        while (i < n->n && bkey_cmp(key, n->keys[i]) >= 0) i++;
        n = n->child[i];
    }
    // 现在在叶子里,找第一个 >= key 的槽位
    int i = 0;
    while (i < n->n && bkey_cmp(n->keys[i], key) < 0) i++;
    slot_out = i;
    return n;
}

bool BPlusTree::search(const String& key, String* out_value) const {
    int slot;
    BNode* leaf = seek_leaf(key, slot);
    if (!leaf) return false;
    if (slot < leaf->n && bkey_cmp(leaf->keys[slot], key) == 0) {
        if (out_value) *out_value = leaf->values[slot];
        return true;
    }
    return false;
}

// ===================== 插入 =====================
bool BPlusTree::insert_internal(BNode* node, const String& key, const String& value,
                                String& up_key, BNode*& up_right) {
    if (node->is_leaf) {
        int i = 0;
        while (i < node->n && bkey_cmp(node->keys[i], key) < 0) i++;
        // 键已存在 → 覆盖
        if (i < node->n && bkey_cmp(node->keys[i], key) == 0) {
            node->values[i] = value;
            up_right = 0;
            return false;
        }
        // 右移腾出槽位 i
        for (int j = node->n; j > i; j--) {
            node->keys[j] = node->keys[j - 1];
            node->values[j] = node->values[j - 1];
        }
        node->keys[i] = key;
        node->values[i] = value;
        node->n++;
        if (node->n <= BTREE_MAX_LEAF) { up_right = 0; return false; }
        // 叶子分裂:n == MAX+1
        int mid = (node->n + 1) / 2;
        BNode* nr = new BNode(true);
        for (int j = mid; j < node->n; j++) {
            nr->keys[j - mid] = node->keys[j];
            nr->values[j - mid] = node->values[j];
        }
        nr->n = node->n - mid;
        nr->next = node->next;
        node->next = nr;        // 把左兄弟接到新右叶子
        node->n = mid;
        up_key = nr->keys[0];
        up_right = nr;
        return true;
    }

    // 内部节点:下降到子节点
    int i = 0;
    while (i < node->n && bkey_cmp(key, node->keys[i]) >= 0) i++;
    BNode* child = node->child[i];
    String child_up; BNode* child_right = 0;
    bool sp = insert_internal(child, key, value, child_up, child_right);
    if (!sp) { up_right = 0; return false; }

    // 把子节点分裂提升上来的键 / 兄弟插到槽位 i
    for (int j = node->n; j > i; j--) {
        node->keys[j] = node->keys[j - 1];
        node->child[j + 1] = node->child[j];
    }
    node->keys[i] = child_up;
    node->child[i + 1] = child_right;
    node->n++;
    if (node->n <= BTREE_MAX_LEAF) { up_right = 0; return false; }

    // 内部节点分裂
    int mid = (node->n + 1) / 2;       // 提升 keys[mid-1]
    String promote = node->keys[mid - 1];
    BNode* nr = new BNode(false);
    for (int j = mid; j < node->n; j++) nr->keys[j - mid] = node->keys[j];
    for (int j = mid; j <= node->n; j++) {
        nr->child[j - mid] = node->child[j];
        node->child[j] = 0;
    }
    nr->n = node->n - mid;
    node->n = mid - 1;
    up_key = promote;
    up_right = nr;
    return true;
}

void BPlusTree::insert(const String& key, const String& value) {
    if (!root_) {
        root_ = new BNode(true);
        root_->keys[0] = key;
        root_->values[0] = value;
        root_->n = 1;
        count_ = 1;
        return;
    }
    // 先尝试覆盖
    String oldv;
    bool existed = search(key, &oldv);
    String up; BNode* up_right = 0;
    bool sp = insert_internal(root_, key, value, up, up_right);
    if (!existed) count_++;
    if (!sp) return;
    // 根分裂:新建一个根
    BNode* nr = new BNode(false);
    nr->keys[0] = up;
    nr->child[0] = root_;
    nr->child[1] = up_right;
    nr->n = 1;
    root_ = nr;
}

// ===================== 删除 =====================
bool BPlusTree::remove_internal(BNode* node, const String& key) {
    if (node->is_leaf) {
        int i = leaf_find(node, key);
        if (i < 0) return false;            // 键不存在
        for (int j = i; j < node->n - 1; j++) {
            node->keys[j] = node->keys[j + 1];
            node->values[j] = node->values[j + 1];
        }
        node->n--;
        return node->n < BTREE_MIN_LEAF;    // 是否下溢
    }
    // 内部节点
    int i = 0;
    while (i < node->n && bkey_cmp(key, node->keys[i]) >= 0) i++;
    BNode* child = node->child[i];
    bool under = remove_internal(child, key);
    if (!under) return false;

    // child 下溢:与兄弟合并(本实现不做借位,合并总能保证不超过上限)
    if (i + 1 <= node->n) {
        // 与右兄弟 child[i+1] 合并
        BNode* R = node->child[i + 1];
        if (child->is_leaf) {
            for (int j = 0; j < R->n; j++) {
                child->keys[child->n + j] = R->keys[j];
                child->values[child->n + j] = R->values[j];
            }
            child->n += R->n;
            child->next = R->next;
        } else {
            child->keys[child->n] = node->keys[i];
            child->n++;
            for (int j = 0; j < R->n; j++) {
                child->keys[child->n + j] = R->keys[j];
                child->child[child->n + j] = R->child[j];
            }
            child->child[child->n + R->n] = R->child[R->n];
            child->n += R->n;
        }
        // 从父节点删掉 keys[i] 与 child[i+1]
        for (int j = i; j < node->n - 1; j++) {
            node->keys[j] = node->keys[j + 1];
            node->child[j + 1] = node->child[j + 2];
        }
        node->n--;
        node->child[node->n + 1] = 0;   // 末尾残留槽置空(有效子为 0..node->n)
        R->is_leaf = true;     // 防止析构递归删子树(子节点已被 child 接管)
        R->n = 0;
        delete R;
    } else {
        // 与左兄弟 child[i-1] 合并
        BNode* L = node->child[i - 1];
        if (L->is_leaf) {
            for (int j = 0; j < child->n; j++) {
                L->keys[L->n + j] = child->keys[j];
                L->values[L->n + j] = child->values[j];
            }
            L->n += child->n;
            L->next = child->next;
        } else {
            L->keys[L->n] = node->keys[i - 1];
            L->n++;
            for (int j = 0; j < child->n; j++) {
                L->keys[L->n + j] = child->keys[j];
                L->child[L->n + j] = child->child[j];
            }
            L->child[L->n + child->n] = child->child[child->n];
            L->n += child->n;
        }
        // 从父节点删掉 keys[i-1] 与 child[i]
        for (int j = i - 1; j < node->n - 1; j++) {
            node->keys[j] = node->keys[j + 1];
            node->child[j] = node->child[j + 1];
        }
        node->n--;
        node->child[node->n + 1] = 0;
        child->is_leaf = true;
        child->n = 0;
        delete child;
    }
    int min_keys = node->is_leaf ? BTREE_MIN_LEAF : (BTREE_MIN_CHILD - 1);
    return node->n < min_keys;
}

bool BPlusTree::remove(const String& key) {
    if (!root_) return false;
    if (!contains(key)) return false;       // 确认存在,免得误删计数
    remove_internal(root_, key);
    count_--;
    // 根退化:内部根只剩一个子指针 → 下沉
    if (!root_->is_leaf && root_->n == 0) {
        BNode* old = root_;
        root_ = old->child[0];
        old->child[0] = 0;
        old->is_leaf = true;
        old->n = 0;
        delete old;
    }
    if (root_ && root_->is_leaf && root_->n == 0) {
        delete root_;
        root_ = 0;
    }
    return true;
}

// ===================== 范围查询 =====================
void BPlusTree::range_query(const String& lo, const String& hi,
                            List<String>& out_keys, List<String>& out_values) const {
    int slot;
    BNode* leaf = seek_leaf(lo, slot);
    while (leaf) {
        for (int i = slot; i < leaf->n; i++) {
            const String& k = leaf->keys[i];
            if (!hi.empty() && bkey_cmp(k, hi) > 0) return;
            if (!lo.empty() && bkey_cmp(k, lo) < 0) { /* 起点之前,跳过 */ continue; }
            out_keys.push(k);
            out_values.push(leaf->values[i]);
        }
        leaf = leaf->next;
        slot = 0;
    }
}

// ===================== 迭代器 =====================
BTreeIter::BTreeIter(const BPlusTree& t) : tree_(t), cur_(0), slot_(0), done_(true) {}

void BTreeIter::seek_first() { seek(String("")); }

void BTreeIter::seek(const String& key) {
    int slot = 0;
    BNode* leaf = tree_.seek_leaf(key, slot);
    cur_ = leaf;
    slot_ = slot;
    if (!cur_ || slot_ >= cur_->n) done_ = true;
    else done_ = false;
}

const String& BTreeIter::value() const {
    static String empty;
    if (done_ || !cur_) return empty;
    return cur_->values[slot_];
}

void BTreeIter::next() {
    if (done_) return;
    slot_++;
    if (slot_ >= cur_->n) {
        cur_ = cur_->next;
        slot_ = 0;
        if (!cur_ || cur_->n == 0) done_ = true;
    }
}

void BTreeIter::prev() {
    // 小规模实现:从最左叶子开始线性回退到当前键的前一个。
    // 数据库规模不大时完全够用,避免为每个叶子维护前驱指针。
    if (done_) return;
    String cur_key = key();
    BNode* leaf = tree_.leftmost_leaf();
    BNode* prev_leaf = 0;
    int prev_slot = -1;
    BNode* scan = leaf;
    while (scan) {
        for (int i = 0; i < scan->n; i++) {
            if (bkey_cmp(scan->keys[i], cur_key) >= 0) {
                // 找到当前位置
                if (prev_slot < 0) { done_ = true; return; }   // 已经是第一个
                cur_ = prev_leaf;
                slot_ = prev_slot;
                return;
            }
            prev_leaf = scan;
            prev_slot = i;
        }
        scan = scan->next;
    }
}

// ===================== 序列化 =====================
// 格式:[magic 4B][count u32] 重复 [klen u16][key][vlen u16][value]
uint8_t* BPlusTree::serialize(int* out_len) const {
    // 先算总长度
    int total = 8;   // magic + count
    BNode* leaf = leftmost_leaf();
    int cnt = 0;
    for (BNode* l = leaf; l; l = l->next) {
        for (int i = 0; i < l->n; i++) {
            total += 2 + l->keys[i].len() + 2 + l->values[i].len();
            cnt++;
        }
    }
    uint8_t* out = new uint8_t[total > 8 ? total : 9];
    if (!out) { if (out_len) *out_len = 0; return 0; }
    out[0] = 'B'; out[1] = 'T'; out[2] = '1'; out[3] = 0;
    out[4] = (uint8_t)(cnt); out[5] = (uint8_t)(cnt >> 8);
    out[6] = (uint8_t)(cnt >> 16); out[7] = (uint8_t)(cnt >> 24);
    int p = 8;
    for (BNode* l = leaf; l; l = l->next) {
        for (int i = 0; i < l->n; i++) {
            int kl = l->keys[i].len(), vl = l->values[i].len();
            out[p++] = (uint8_t)(kl); out[p++] = (uint8_t)(kl >> 8);
            for (int j = 0; j < kl; j++) out[p++] = (uint8_t)l->keys[i][j];
            out[p++] = (uint8_t)(vl); out[p++] = (uint8_t)(vl >> 8);
            for (int j = 0; j < vl; j++) out[p++] = (uint8_t)l->values[i][j];
        }
    }
    if (out_len) *out_len = p;
    return out;
}

int BPlusTree::deserialize(const uint8_t* data, int len) {
    clear();
    if (!data || len < 8) return 0;
    if (data[0] != 'B' || data[1] != 'T' || data[2] != '1') return 0;
    int cnt = (int)data[4] | ((int)data[5] << 8) | ((int)data[6] << 16) | ((int)data[7] << 24);
    int p = 8;
    int imported = 0;
    for (int i = 0; i < cnt && p + 2 <= len; i++) {
        int kl = data[p] | (data[p + 1] << 8); p += 2;
        if (p + kl > len) break;
        String key((const char*)(data + p), kl); p += kl;
        if (p + 2 > len) break;
        int vl = data[p] | (data[p + 1] << 8); p += 2;
        if (p + vl > len) break;
        String val((const char*)(data + p), vl); p += vl;
        insert(key, val);
        imported++;
    }
    return imported;
}

// ===================== 自检 =====================
int btree_self_test() {
    int fail = 0;

    // 1) 插入 1000 个键,全部能找到,值正确
    {
        BPlusTree t;
        for (int i = 0; i < 1000; i++) {
            char k[16]; ksprintf(k, sizeof(k), "key%04d", i);
            char v[16]; ksprintf(v, sizeof(v), "val%d", i * 7);
            t.insert(String(k), String(v));
        }
        if (t.count() != 1000) fail++;
        for (int i = 0; i < 1000; i++) {
            char k[16]; ksprintf(k, sizeof(k), "key%04d", i);
            String got;
            if (!t.search(String(k), &got)) { fail++; continue; }
            char want[16]; ksprintf(want, sizeof(want), "val%d", i * 7);
            if (strcmp(got.c_str(), want) != 0) fail++;
        }
        // 覆盖(注意 ksprintf 不补零,键名与插入时一致)
        char ok0[16]; ksprintf(ok0, sizeof(ok0), "key%04d", 0);
        t.insert(String(ok0), String("overwritten"));
        String got;
        if (!t.search(String(ok0), &got) || strcmp(got.c_str(), "overwritten") != 0) fail++;
        if (t.count() != 1000) fail++;   // 覆盖不增加计数
    }

    // 2) 乱序插入(制造大量分裂)
    {
        BPlusTree t;
        for (int i = 999; i >= 0; i--) {
            char k[16]; ksprintf(k, sizeof(k), "k%04d", i);
            char v[8]; ksprintf(v, sizeof(v), "%d", i);
            t.insert(String(k), String(v));
        }
        for (int i = 0; i < 1000; i++) {
            char k[16]; ksprintf(k, sizeof(k), "k%04d", i);
            if (!t.contains(String(k))) fail++;
        }
    }

    // 3) 范围查询
    {
        BPlusTree t;
        for (int i = 1; i <= 10; i++) {
            char k[8]; ksprintf(k, sizeof(k), "n%d", i);
            char v[8]; ksprintf(v, sizeof(v), "v%d", i);
            t.insert(String(k), String(v));
        }
        List<String> keys, vals;
        t.range_query(String("n3"), String("n7"), keys, vals);
        if (keys.size() != 5) fail++;       // n3..n7
        if (keys.size() == 5 && strcmp(keys[0].c_str(), "n3") != 0) fail++;
        if (keys.size() == 5 && strcmp(keys[4].c_str(), "n7") != 0) fail++;
    }

    // 4) 删除:删掉一半,验证剩余与缺失
    {
        BPlusTree t;
        for (int i = 0; i < 100; i++) {
            char k[12]; ksprintf(k, sizeof(k), "d%03d", i);
            char v[8]; ksprintf(v, sizeof(v), "v%d", i);
            t.insert(String(k), String(v));
        }
        for (int i = 0; i < 100; i += 2) {      // 删偶数
            char k[12]; ksprintf(k, sizeof(k), "d%03d", i);
            if (!t.remove(String(k))) fail++;
        }
        if (t.count() != 50) fail++;
        for (int i = 0; i < 100; i++) {
            char k[12]; ksprintf(k, sizeof(k), "d%03d", i);
            bool has = t.contains(String(k));
            if (i % 2 == 0 && has) fail++;       // 偶数应已删
            if (i % 2 == 1 && !has) fail++;      // 奇数应还在
        }
        // 再删不存在的键应返回 false
        char ghost[12]; ksprintf(ghost, sizeof(ghost), "d999");
        if (t.remove(String(ghost))) fail++;
    }

    // 5) 序列化 → 反序列化,内容一致
    {
        BPlusTree t;
        for (int i = 0; i < 50; i++) {
            char k[12]; ksprintf(k, sizeof(k), "s%03d", i);
            char v[12]; ksprintf(v, sizeof(v), "sv%d", i * i);
            t.insert(String(k), String(v));
        }
        int len = 0;
        uint8_t* blob = t.serialize(&len);
        BPlusTree t2;
        int imp = t2.deserialize(blob, len);
        delete[] blob;
        if (imp != 50) fail++;
        if (t2.count() != 50) fail++;
        for (int i = 0; i < 50; i++) {
            char k[12]; ksprintf(k, sizeof(k), "s%03d", i);
            String got;
            if (!t2.search(String(k), &got)) { fail++; continue; }
            char want[12]; ksprintf(want, sizeof(want), "sv%d", i * i);
            if (strcmp(got.c_str(), want) != 0) fail++;
        }
    }

    // 6) 迭代器顺序遍历
    {
        BPlusTree t;
        for (int i = 1; i <= 5; i++) {
            char k[8]; ksprintf(k, sizeof(k), "m%d", i);
            t.insert(String(k), String(k));
        }
        BTreeIter it(t);
        it.seek_first();
        int sum = 0;
        while (!it.done()) {
            sum += atoi(it.key().c_str() + 1);
            it.next();
        }
        if (sum != 1 + 2 + 3 + 4 + 5) fail++;
    }

    return fail;
}

} // namespace database
} // namespace nefu
