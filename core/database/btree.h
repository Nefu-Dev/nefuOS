// nefuOS 嵌入式数据库 —— B+ 树(BPlus Tree)
//
// B+ 树是关系型数据库索引的经典结构:
//   - 所有真实数据(键值对)只存放在叶子节点;内部节点只存 "路由键",扇出大、树高低。
//   - 叶子节点之间用链表串起来,所以范围查询(range query)只要定位到起点叶子,
//     然后顺着链表走即可,不用反复从根往下查。
//   - 阶数 ORDER=4:内部节点最多 ORDER-1=3 个键、ORDER=4 个子指针;
//     叶子节点最多 3 个键值对。插入导致叶子满了就分裂;删除导致节点过空就合并或借位。
//
// 键 / 值都用二进制安全的字符串(nefu::String)承载,按字典序(字节序)比较。
// 不依赖 STL / 异常 / RTTI。
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace database {

// B+ 树阶数:子节点数上限。3 阶或 4 阶都适合教学,这里取 4。
const int BTREE_ORDER = 4;
// 叶子节点最多存多少个键值对(= ORDER-1)
const int BTREE_MAX_LEAF = BTREE_ORDER - 1;
// 叶子节点最少保留多少个键值对(删除时低于此值触发合并/借位)
const int BTREE_MIN_LEAF = (BTREE_ORDER + 1) / 2 - 1;   // =1 (ORDER=4)
// 内部节点最少保留多少个子指针
const int BTREE_MIN_CHILD = (BTREE_ORDER + 1) / 2;       // =2 (ORDER=4)

// ---- 节点结构 ----
// 内部节点和叶子节点共用一个结构:内部节点用 child[],叶子节点用 values[]+next。
struct BNode {
    bool   is_leaf;
    int    n;                 // 当前键的数量
    String keys[BTREE_MAX_LEAF + 1];   // 键(多开一个槽,方便分裂时临时放)
    // 内部节点:子指针(child[0..n])
    BNode* child[BTREE_ORDER + 1];
    // 叶子节点:与 keys[i] 对应的 value,以及向右的叶子链表
    String values[BTREE_MAX_LEAF + 1];
    BNode* next;              // 叶子链表后继(仅叶子用)

    BNode(bool leaf);
    ~BNode();
};

// ---- B+ 树 ----
class BPlusTree {
public:
    BPlusTree();
    ~BPlusTree();

    // 插入 / 覆盖一个键值。key 不能为空串。
    void insert(const String& key, const String& value);

    // 精确查找。找到返回 true 并把 value 写入 out(可为空指针)。
    bool search(const String& key, String* out_value) const;

    // 是否存在某个键
    bool contains(const String& key) const { return search(key, 0); }

    // 删除一个键。删除成功返回 true。
    bool remove(const String& key);

    // 范围查询:收集 [lo, hi] 闭区间内的所有键值对(字典序)。
    // lo/hi 传空串表示开放边界(lo 空 = 从最小键开始,hi 空 = 到最大键结束)。
    void range_query(const String& lo, const String& hi,
                     List<String>& out_keys, List<String>& out_values) const;

    // 键的总数
    int count() const { return count_; }

    // 清空整棵树
    void clear();

    // 找到 >= key 的最小叶子节点(用于游标 / 索引扫描)。
    // 返回叶子节点指针与该节点内 >= key 的槽位下标。
    BNode* seek_leaf(const String& key, int& slot_out) const;

    // 最左叶子(全表扫描起点)
    BNode* leftmost_leaf() const;

    // ---- 序列化 ----
    // 把整棵树按叶子链表顺序导出为紧凑字节流(长度前缀编码)。
    // 调用者用 delete[] 释放。out_len 输出字节数。
    uint8_t* serialize(int* out_len) const;
    // 从 serialize() 的字节流重建一棵树(会先清空现有内容)。返回导入键数。
    int deserialize(const uint8_t* data, int len);

private:
    // 递归在子树里插入;若发生分裂,会把提升键与新兄弟节点写到 up_key/up_right。
    // 返回 true 表示该节点发生了上溢需要分裂。
    bool insert_internal(BNode* node, const String& key, const String& value,
                         String& up_key, BNode*& up_right);

    // 递归删除(自底向上合并/借位)
    bool remove_internal(BNode* node, const String& key);

    // 在叶子节点里找 key 的下标,找不到返回 -1
    static int leaf_find(BNode* leaf, const String& key);

    BNode* root_;
    int    count_;
public:
    BNode* debug_root() const { return root_; }
private:
};

// ---- 迭代器(顺序扫描叶子链表) ----
class BTreeIter {
public:
    BTreeIter(const BPlusTree& t);

    // 把游标定位到 >= key 的第一个键;若 key 比所有键都大,则 done。
    void seek(const String& key);
    // 定位到第一个键
    void seek_first();

    bool done() const { return done_; }
    // 当前键 / 值(仅 !done 时有效)
    const String& key() const { return cur_->keys[slot_]; }
    const String& value() const;

    void next();    // 后移
    void prev();    // 前移(需要顺着链表回到前驱叶子,本实现用线性回退,小规模足够)

private:
    const BPlusTree& tree_;
    BNode* cur_;
    int    slot_;
    bool   done_;
};

// 模块自检:返回失败断言数(0 表示全部通过)
int btree_self_test();

} // namespace database
} // namespace nefu
