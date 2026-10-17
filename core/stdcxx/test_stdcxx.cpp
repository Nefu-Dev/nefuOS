// nefuOS stdcxx compile-verification program (host build check only)
#include "stdcxx_all.h"
#include <stdio.h>
using namespace nefu::stdx;

int fails = 0;
#define CHECK(label, expr) do { if (!(expr)) { printf("FAIL: %s\n", label); ++fails; } } while (0)

int main() {
    // vector
    vector<int> v;
    for (int i = 0; i < 100; i++) v.push_back(i);
    CHECK("vector size", v.size() == 100);
    CHECK("vector[50]", v[50] == 50);
    v.resize(120, -1);
    CHECK("vector resize", v[119] == -1);

    // array
    array<int, 4> a; a.fill(7);
    CHECK("array size", a.size() == 4);
    CHECK("array[3]", a[3] == 7);

    // string
    string s = "hello";
    s += " world";
    CHECK("string size", s.size() == 11);
    CHECK("string find", s.find('w') == 6);
    string t = s.substr(0, 5);
    CHECK("string substr", t == "hello");
    string u = s + "!";
    CHECK("string cat", u == "hello world!");

    // list
    list<int> l;
    for (int i = 0; i < 10; i++) l.push_back(i);
    CHECK("list size", l.size() == 10);
    CHECK("list front", l.front() == 0);
    CHECK("list back", l.back() == 9);
    l.pop_back(); l.pop_front();
    CHECK("list pop", l.front() == 1 && l.back() == 8 && l.size() == 8);

    // map / set
    map<int, int> m;
    for (int i = 0; i < 50; i++) m[i] = i * 2;
    m[25] = 999;
    CHECK("map[25]", m[25] == 999);
    CHECK("map[0]", m[0] == 0);
    CHECK("map[49]", m[49] == 98);
    CHECK("map contains", m.contains(30) && !m.contains(100));
    m.erase(25);
    CHECK("map erase", !m.contains(25));
    CHECK("map size", m.size() == 49);

    set<int> st;
    for (int i = 0; i < 30; i++) st.insert(i % 10);
    CHECK("set size", st.size() == 10);
    CHECK("set contains", st.contains(5) && !st.contains(20));

    // queue / stack
    queue<int> q;
    for (int i = 0; i < 5; i++) q.push(i);
    CHECK("queue front", q.front() == 0);
    CHECK("queue back", q.back() == 4);
    stack<int> sk;
    for (int i = 0; i < 5; i++) sk.push(i);
    CHECK("stack top", sk.top() == 4);

    // algorithm
    vector<int> sv; sv.push_back(3); sv.push_back(1); sv.push_back(2); sv.push_back(0);
    sort(sv.begin(), sv.end());
    CHECK("sort", sv[0] == 0 && sv[3] == 3);
    int acc = accumulate(sv.begin(), sv.end(), 0);
    CHECK("accumulate", acc == 6);

    // pair / make_pair / swap
    pair<int, string> p = make_pair(1, "one");
    CHECK("pair first", p.first == 1);
    CHECK("pair second", p.second == "one");
    int x = 1, y = 2; swap(x, y);
    CHECK("swap", x == 2 && y == 1);

    // numeric_limits / type_traits
    CHECK("numeric_limits max", numeric_limits<int>::max() == 2147483647);
    CHECK("is_integral", is_integral<int>::value && !is_integral<float>::value);

    // unique_ptr
    unique_ptr<int> up(new int(42));
    CHECK("unique_ptr", up && *up == 42);

    printf("stdcxx self-test: %s\n", fails == 0 ? "PASS" : "FAIL");
    printf("failures=%d\n", fails);
    return fails;
}
