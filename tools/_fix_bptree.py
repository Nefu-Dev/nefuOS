# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\dblib\bptree.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''namespace nefu {
namespace dbx {

BpTree::~BpTree() { destroy(root_); }'''
new = '''namespace nefu {
namespace dbx {

// Node 现为公有嵌套类型：加别名使类外实现书写简洁
using Node = BpTree::Node;

BpTree::~BpTree() { destroy(root_); }'''
assert s.count(old) == 1, 'using'
s = s.replace(old, new)

s = s.replace('BpTree::Node* BpTree::insert_rec(BpTree::Node* n, int key, int value, bool& ok) {',
              'Node* BpTree::insert_rec(Node* n, int key, int value, bool& ok) {')
s = s.replace('void BpTree::split_child(BpTree::Node* parent, int idx, BpTree::Node* child) {',
              'void BpTree::split_child(Node* parent, int idx, Node* child) {')
s = s.replace('int BpTree::find_idx(BpTree::Node* parent, BpTree::Node* child) {',
              'int BpTree::find_idx(Node* parent, Node* child) {')
s = s.replace('bool BpTree::remove_rec(BpTree::Node* n, int key) {',
              'bool BpTree::remove_rec(Node* n, int key) {')
s = s.replace('void BpTree::collect(BpTree::Node* n, std::vector<std::pair<int, int> >& out) const {',
              'void BpTree::collect(Node* n, std::vector<std::pair<int, int> >& out) const {')
s = s.replace('int BpTree::count_rec(BpTree::Node* n) const {',
              'int BpTree::count_rec(Node* n) const {')
s = s.replace('int BpTree::height_rec(BpTree::Node* n) const {',
              'int BpTree::height_rec(Node* n) const {')
s = s.replace('void BpTree::destroy(BpTree::Node* n) {',
              'void BpTree::destroy(Node* n) {')

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK restore bare signatures')
