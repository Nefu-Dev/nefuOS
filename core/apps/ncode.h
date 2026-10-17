// =============================================================================
//  ncode.h — nefuOS ".ncode" scripting language (nefu) runtime interface
// -----------------------------------------------------------------------------
//  .ncode is the npkg package scripting format. It is a small interpreted
//  language with the syntax:
//      #nefu                 optional first-line marker
//      import<sysapi>        import a library (sysapi / std)
//      func add(a,b){...}    function definition
//      main() { ... }        entry point (required)
//  The interpreter exposes the whole nefu::sysapi API set as builtin
//  functions, plus std helpers and start_r_os() (see ncode.cpp).
//
//  Constraints: no STL / no exceptions / no RTTI — same as the rest of the
//  kernel. Memory via kalloc/kfree and nefu::String / nefu::List.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace nefu {

struct FSNode;

// ---- output sink ----
// The interpreter never touches the terminal directly; the caller provides a
// sink. In the OS, terminal.cpp wires it to term_ext_print(TermState*).
struct NCOut {
    void (*print)(void* ud, const char* s);   // write a chunk (no newline)
    void* ud;
    void p(const char* s) { if (print) print(ud, s ? s : ""); }
    void pln(const char* s) { if (print) { print(ud, s ? s : ""); print(ud, "\n"); } }
    void pln() { if (print) print(ud, "\n"); }
};

// Run a .ncode source buffer. path is used only for error messages / sandbox
// naming. args/argc are the extra command-line arguments visible via args().
// Returns false on compile/runtime error (details printed through out).
bool ncode_run_source(NCOut* out, const char* src, int srclen,
                      const char* path, const char** args, int argc);

// Run a .ncode file from the VFS.
bool ncode_run_file(NCOut* out, FSNode* f, const char** args, int argc);

// Interpreter self test (no GUI required). Returns failure count.
int ncode_self_test();

} // namespace nefu
