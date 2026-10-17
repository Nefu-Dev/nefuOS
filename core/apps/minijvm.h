// nefuOS mini JVM - a small Java class-file interpreter for the nefuOS kernel.
//
// This is the "real JDK on nefuOS" runner (jdk-integration-plan.md Phase 2):
// instead of a full OpenJDK port, minijvm parses javac 8..21 class files
// (CAFEBABE magic, constant pool, fields, methods, Code attribute + exception
// table) and interprets the full JVM instruction set for typical programs:
//   - int/long/float/double arithmetic (soft-float, no hardware FPU)
//   - control flow, arrays, static + instance fields, same-class methods
//   - exceptions: try/catch/finally via the Code exception table + athrow
//   - built-in classes: java/lang (Object, String, StringBuilder, wrappers,
//     Math full set, System), java/util (ArrayList, HashMap, HashSet,
//     Iterator, Map.Entry, Arrays, Collections), java/io (PrintStream, File,
//     FileInputStream/OutputStream, ByteArray streams) over the VFS hooks
//   - mark-sweep GC over a bounded object table
//
// Design rules (mirror the kernel):
//   - no libc math: all transcendental functions are implemented here with
//     soft-float double arithmetic (GCC soft-float on bare metal)
//   - bounded memory: fixed frames/stack, object table + free list
//   - errors are reported through the out callback as text, return code != 0
#pragma once

#include <stdint.h>

namespace nefu {

// Run the main() method of a Java class file.  The class data must start with
// the CAFEBABE magic.  Every output line (System.out.print/println) is
// appended to the out callback.  argv/argc become the String[] main argument
// (pass argc=0 / argv=0 for none).  Returns 0 on success, non-zero on error.
int minijvm_run_class(const uint8_t* class_data, uint32_t len,
                      void (*out)(const char* s, void* ud), void* ud,
                      int argc = 0, const char* const* argv = 0);

// ---- optional VFS hooks (java/io over the nefuOS file system) -------------
// The terminal app registers these through minijvm_set_vfs(); the headless
// test does not, so java/io calls fail with a clear message there.  A file is
// addressed by its absolute path ("/home/foo.txt").  The read/write contract
// mirrors POSIX pread/pwrite: off is the byte offset, returns bytes done.

struct MiniJvmFile {
    bool  is_dir;
    uint32_t size;
};

struct MiniJvmVfs {
    int  (*stat)(void* ud, const char* path, MiniJvmFile* out);   // 0 ok, 1 missing
    int  (*read)(void* ud, const char* path, uint8_t* buf, uint32_t off, uint32_t len);
    int  (*write)(void* ud, const char* path, const uint8_t* buf, uint32_t off, uint32_t len);
    int  (*create)(void* ud, const char* path);                    // truncate/create file
    int  (*mkdir)(void* ud, const char* path);
    int  (*remove)(void* ud, const char* path);
    int  (*list)(void* ud, const char* path, char* out, int out_sz); // newline-separated names
    void* ud;
};

void minijvm_set_vfs(const MiniJvmVfs* hooks);

// Simple statistics a caller may print after a run (GC counts etc).
struct MiniJvmStats {
    int gc_runs;
    int objects_used;      // high-water mark of the object table
    int max_frames;        // deepnest recursion seen
};
void minijvm_stats(MiniJvmStats* out);

} // namespace nefu
