// nefuOS JSON library — parse + serialize
// Portable: no STL, no exceptions, no dynamic allocation beyond String/List.
// Numbers are stored as int64; fractional numbers keep their raw text in
// Value::s with type JSON_INT (caller can re-parse if needed).
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace json {

enum Type {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_INT,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

struct Value;
struct Member;

struct Value {
    Type type;
    bool b;          // JSON_BOOL
    long long i;     // JSON_INT
    String s;        // JSON_STRING (or raw text for unparsed numbers)
    List<Value> arr; // JSON_ARRAY
    List<Member> obj;// JSON_OBJECT (ordered)

    Value() : type(JSON_NULL), b(false), i(0) {}
    Value(bool v) : type(JSON_BOOL), b(v), i(0) {}
    Value(long long v) : type(JSON_INT), b(false), i(v) {}
    Value(int v) : type(JSON_INT), b(false), i(v) {}
    Value(const char* v) : type(JSON_STRING), b(false), i(0), s(v) {}
    Value(const String& v) : type(JSON_STRING), b(false), i(0), s(v) {}

    // predicates
    bool is_null() const { return type == JSON_NULL; }
    bool is_bool() const { return type == JSON_BOOL; }
    bool is_int() const { return type == JSON_INT; }
    bool is_string() const { return type == JSON_STRING; }
    bool is_array() const { return type == JSON_ARRAY; }
    bool is_object() const { return type == JSON_OBJECT; }

    // array
    int size() const;
    const Value& at(int idx) const { return arr[idx]; }
    Value& at(int idx) { return arr[idx]; }

    // object (defined in json.cpp so Member is complete)
    const Value* get(const char* key) const;
    Value* get(const char* key);
    bool has(const char* key) const;
    void set(const char* key, const Value& v);
    void push(const Value& v) { arr.push(v); }
    const char* get_str(const char* key, const char* def = "") const;
    long long get_int(const char* key, long long def = 0) const;
    bool get_bool(const char* key, bool def = false) const;
};

struct Member {
    String key;
    Value val;
    Member() {}
    Member(const String& k, const Value& v) : key(k), val(v) {}
};

// parse a JSON document. Returns false + *err on error (err may be null).
bool parse(const char* text, Value& out, const char** err = 0);
bool parse(const String& text, Value& out, const char** err = 0);

// serialize. pretty adds 2-space indentation.
void to_string(const Value& v, String& out, bool pretty = false);
// short one-line form (same as to_string with pretty=false)
String dump(const Value& v);

// helpers
long long  str_to_i64(const char* s);           // parse int64 (used for numbers)
void       i64_to_str(long long v, String& out);

} // namespace json
} // namespace nefu
