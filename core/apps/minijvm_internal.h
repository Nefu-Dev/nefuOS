// nefuOS mini JVM - internal shared definitions.
// Split across minijvm_core.cpp (parsing/GC/helpers), minijvm.cpp (interpreter
// + entry) and minijvm_builtin.cpp (built-in class dispatch) so each TU stays
// small.  Soft double math is header-only (minijvm_math.h).
#pragma once

#include <stdint.h>
#include <string.h>
#include "minijvm.h"
#include "minijvm_math.h"

namespace nefu {

// ---------------- bounds (keep the whole VM in fixed memory) ----------------
const int MAX_CP      = 512;
const int MAX_FIELDS  = 64;
const int MAX_METHODS = 128;
const int MAX_CODE    = 16384;
const int MAX_LOCALS  = 96;
const int MAX_STACK   = 1024;
const int MAX_FRAMES  = 64;
const int MAX_OBJS    = 512;
const int MAX_ARR     = 1024;
const int MAX_STEPS   = 8000000u;
const int MAX_STATICS = 64;
const int MAX_EXC     = 32;

// ---------------- stack value ----------------
enum { K_INT = 0, K_LONG = 1, K_F32 = 2, K_F64 = 3, K_REF = 4, K_NULL = 5 };

struct Val {
    int64_t v;
    uint8_t kind;
};

// ---------------- heap object ----------------
enum {
    OBJ_FREE = 255,
    OBJ_INST = 0,        // generic instance: cls + flds[] (user classes, throwables)
    OBJ_PRINTSTREAM = 1,
    OBJ_STRING = 2,
    OBJ_INTARR = 3,      // int32 elements (int/float bits/byte/char/short/bool)
    OBJ_STRARR = 4,      // object refs (int32)
    OBJ_LONGARR = 5,     // int64 elements (long/double bits)
    OBJ_BOX = 6,         // boxed primitive: cls = tag char, val = bits
    OBJ_STRBUF = 7,      // StringBuilder / StringBuffer
    OBJ_ARR = 8,         // ArrayList
    OBJ_MAP = 9,         // HashMap
    OBJ_SET = 10,        // HashSet
    OBJ_ITR = 11,        // Iterator
    OBJ_CLASS = 12,      // java/lang/Class: cls = dotted class name
    OBJ_FILE = 13,       // java/io/File: cls = path
    OBJ_STREAM = 14      // IO streams: cls = type tag
};

struct Obj {
    uint8_t kind;
    uint8_t mark;        // GC mark
    uint16_t fld_count;  // OBJ_INST: number of instance field slots
    int32_t len;         // string/buffer/array length
    int32_t cap;         // capacity (arrays / string buffers)
    int32_t next;        // free-list link (-1 = none)
    char*   cls;         // INST: class name / BOX: tag / FILE: path / STREAM: tag
    char*   str;         // STRING / STRBUF: chars
    int32_t* arr;        // INTARR data / STRARR refs / ARR refs / SET keys / MAP k/v pairs
    int64_t* larr;       // LONGARR data
    int64_t* flds;       // INST field values (bits / refs)
    int64_t  val;        // BOX value / ITR pos / misc
};

// ---------------- constant pool ----------------
struct CpEntry {
    uint8_t tag;                 // 0 = unused
    uint16_t a, b;               // tag-dependent u2 indexes
    int32_t ival;                // CONSTANT_Integer / Float bits
    int64_t  lval;               // CONSTANT_Long / Double bits
    char*    utf8;               // CONSTANT_Utf8 (NUL-terminated)
};

// ---------------- class model ----------------
struct FieldInfo {
    char* name;
    char* desc;
    bool  is_static;
    int   slot;                  // statics[] slot (static) or instance-field slot
};

struct ExcInfo {
    uint16_t start, end, handler, catch_type;   // catch_type = cp idx, 0 = any
};

struct MethodInfo {
    char* name;
    char* desc;
    bool  is_static;
    uint16_t max_stack, max_locals;
    uint32_t code_len;
    uint8_t* code;
    int      exc_count;
    ExcInfo  exc[MAX_EXC];
};

struct ClassInfo {
    uint16_t cp_count;
    CpEntry cp[MAX_CP];
    char*   this_name;
    int     field_count;
    FieldInfo fields[MAX_FIELDS];
    int     inst_field_count;
    Val     statics[MAX_STATICS];
    int     method_count;
    MethodInfo methods[MAX_METHODS];
    int     main_index;
    int     clinit_index;
    bool    clinit_done;
};

// ---------------- runtime ----------------
struct Frame {
    int method;
    int pc;
    int stack_base;
};

struct VM {
    ClassInfo cls;
    Val  stack[MAX_STACK];
    int  sp;
    Val* locals;                 // [MAX_FRAMES * MAX_LOCALS]
    Frame frames[MAX_FRAMES];
    int  depth;
    uint32_t steps;
    Obj  objs[MAX_OBJS];
    int  obj_count;
    int  free_head;
    void (*out)(const char*, void*);
    void* ud;
    int  err;
    char errbuf[160];
    int64_t exc_ref;
    bool has_exc;
    bool done;
    int  exit_code;
    int64_t guards[16];
    int  guard_n;
    uint64_t rng;
    int  gc_runs;
    int  max_depth;
    const MiniJvmVfs* vfs;
};

// ---------------- byte readers (big-endian) ----------------
static inline uint8_t rd_u1(const uint8_t* p) { return p[0]; }
static inline uint16_t rd_u2(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd_u4(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline int32_t rd_s4(const uint8_t* p) { return (int32_t)rd_u4(p); }

// ---------------- value helpers ----------------
static inline Val V_INT(int64_t x) { Val v; v.v = x; v.kind = K_INT; return v; }
static inline Val V_LONG(int64_t x) { Val v; v.v = x; v.kind = K_LONG; return v; }
static inline Val V_REF(int64_t o) { Val v; v.v = o; v.kind = K_REF; return v; }
static inline Val V_NULL() { Val v; v.v = 0; v.kind = K_NULL; return v; }
static inline Val V_F32(float f) { Val v; v.v = (uint64_t)mj_f_to_bits(f); v.kind = K_F32; return v; }
static inline Val V_F64(double d) { Val v; v.v = mj_d_to_bits(d); v.kind = K_F64; return v; }

static inline int32_t i32(int64_t v) { return (int32_t)(uint32_t)v; }
static inline int64_t sext32(int64_t v) { return (int64_t)(int32_t)(uint32_t)v; }

// ---------------- core (minijvm_core.cpp) ----------------
void vm_set_err(VM* vm, const char* msg);
void vm_emit(VM* vm, const char* s);
void vm_emit_int(VM* vm, int64_t v);
void vm_emit_char(VM* vm, int c);
void vm_emit_f64(VM* vm, int64_t bits);
void format_double_into(char* out, int cap, double d);   // Java-style 6-digit formatting
void vm_push(VM* vm, Val v);
Val  vm_pop(VM* vm);
Val  vm_peek(VM* vm, int n);

Obj* vm_new_obj(VM* vm, uint8_t kind);
Obj* vm_obj_at(VM* vm, int64_t idx);
void vm_gc(VM* vm);
void vm_throw(VM* vm, const char* cls, const char* msg);
void vm_guard(VM* vm, int64_t idx);

int64_t mk_string(VM* vm, const char* s, int len);
int64_t mk_stringz(VM* vm, const char* s);
const char* obj_str(Obj* o);
int64_t str_hash(VM* vm, int64_t ref);
bool str_equals(VM* vm, int64_t a, int64_t b);
Val box_value(VM* vm, char tag, int64_t bits);

bool desc_is_ref(const char* d);
int desc_arg_slots(const char* d);
int find_method(ClassInfo* c, const char* name, const char* desc);
bool is_throwable_owner(const char* o);
bool is_throwable_like(const char* o);   // known list or name ends with Exception/Error/Throwable
bool is_throwable_match(const char* o, const char* c);
bool type_compatible(VM* vm, Obj* o, const char* target);
const char* obj_class_name(VM* vm, Obj* o);

const char* cp_utf8(ClassInfo* c, uint16_t idx);
const char* cp_class_name(ClassInfo* c, uint16_t idx);
const char* cp_name(ClassInfo* c, uint16_t nat_idx);
const char* cp_desc(ClassInfo* c, uint16_t nat_idx);
bool parse_class(const uint8_t* p, uint32_t len, ClassInfo* c);

int32_t f2i_sat(double d);
int64_t f2l_sat(double d);
int call_method(VM* vm, int mi, int nargs, bool has_this);
int find_handler(VM* vm, MethodInfo* m, int pc, const char* ecls);
int propagate_exc(VM* vm);

// ---------------- class dispatch (minijvm_builtin.cpp) ----------------
int invoke_builtin(VM* vm, const char* owner, const char* name, const char* desc, bool has_this);

// ---------------- globals (minijvm.cpp) ----------------
extern const MiniJvmVfs* g_minijvm_vfs_hooks;

} // namespace nefu
