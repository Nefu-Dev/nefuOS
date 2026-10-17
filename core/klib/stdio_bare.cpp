// core/klib/stdio_bare.cpp
//
// Bare-kernel stdio shims for the C FILE APIs.  The host build's debugging
// code (minijs.cpp trace lines etc.) calls fprintf/fputs/fwrite/fflush/fputc
// unconditionally; the bare kernel links no CRT.  fprintf/fputs are already
// provided by backends/bare/bare_libc.cpp, so this file only fills the
// remaining three (fwrite/fflush/fputc), routing output to the COM1 debug
// port.  Compiled to an empty TU on the host build (NEFU_BARE undefined), so
// the host keeps using the real CRT versions.
#ifdef NEFU_BARE
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace {

static inline void sb_outb(uint16_t port, uint8_t v) {
    __asm__ __volatile__("outb %0,%1" :: "a"(v), "Nd"(port));
}
static inline uint8_t sb_inb(uint16_t port) {
    uint8_t r;
    __asm__ __volatile__("inb %1,%0" : "=a"(r) : "Nd"(port));
    return r;
}
static void sb_putc(char c) {
    while (!(sb_inb(0x3F8 + 5) & 0x20)) {}
    sb_outb(0x3F8, (uint8_t)c);
}
static void sb_write(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) sb_putc(s[i]);
}

} // namespace

namespace nefu { int kvsnprintf(char* buf, size_t bufsz, const char* fmt, va_list ap); }

// fprintf / fputs live in backends/bare/bare_libc.cpp (writes to COM1 via
// its own dbgcon()): intentionally NOT redefined here to avoid duplicates.

extern "C" int fputc(int c, void* stream) {
    (void)stream;
    sb_putc((char)c);
    return c;
}

extern "C" size_t fwrite(const void* ptr, size_t sz, size_t nm, void* stream) {
    (void)stream;
    size_t total = sz * nm;
    if (total > 0) sb_write((const char*)ptr, total);
    return total;
}

extern "C" int fflush(void* stream) { (void)stream; return 0; }

// C library symbols referenced by guest-oriented host code (minijs.cpp etc.)
// that must stay dead in the bare kernel. If a future build links them for
// real, replace these with real implementations instead of calling them.
extern "C" char* getenv(const char* name) { (void)name; return 0; }
extern "C" void* fopen(const char* path, const char* mode) { (void)path; (void)mode; return 0; }
extern "C" int fclose(void* stream) { (void)stream; return 0; }

// ---------------------------------------------------------------------------
// Windows API import stubs.
//
// mingw compiles calls to the Win32 API as `call __imp_Foo` (IAT slot loads),
// so the bare kernel's ld needs one `__imp_*` symbol per API used anywhere in
// the tree.  Debug/experimental code in minijs.cpp (and possibly other files)
// calls these unconditionally; the functions are no-ops on the bare kernel and
// only exist so the link succeeds.  The host build never compiles this file
// (NEFU_BARE undefined), so the real CRT/Win32 behaviour is unaffected.
namespace {
__attribute__((used)) static unsigned long __stdcall stub_ul_0(void) { return 0UL; }
__attribute__((used)) static void* __stdcall stub_p_minus1(void) { return (void*)(uintptr_t)-1; }
__attribute__((used)) static void* __stdcall stub_p_0(void) { return (void*)0; }
__attribute__((used)) static long __stdcall stub_l_0(void) { return 0L; }
__attribute__((used)) static void __stdcall stub_v(void) {}
__attribute__((used)) static unsigned long __stdcall stub_ul_1(void) { return 1UL; }
} // namespace

extern "C" {
void* __imp_GetCurrentThreadId = (void*)(uintptr_t)&stub_ul_0;
void* __imp_CreateFileA = (void*)(uintptr_t)&stub_p_minus1;
void* __imp_WriteFile = (void*)(uintptr_t)&stub_ul_1;
void* __imp_CloseHandle = (void*)(uintptr_t)&stub_ul_1;
void* __imp_SetFilePointer = (void*)(uintptr_t)&stub_l_0;
void* __imp_Sleep = (void*)(uintptr_t)&stub_v;
void* __imp_CreateThread = (void*)(uintptr_t)&stub_p_0;
void* __imp_WaitForSingleObject = (void*)(uintptr_t)&stub_l_0;
void* __imp_OpenThread = (void*)(uintptr_t)&stub_p_0;
void* __imp_SuspendThread = (void*)(uintptr_t)&stub_l_0;
void* __imp_ResumeThread = (void*)(uintptr_t)&stub_l_0;
void* __imp_GetThreadContext = (void*)(uintptr_t)&stub_l_0;
void* __imp_AddVectoredExceptionHandler = (void*)(uintptr_t)&stub_p_0;
void* __imp_GetModuleHandleA = (void*)(uintptr_t)&stub_p_0;
void* __imp_GetModuleHandleW = (void*)(uintptr_t)&stub_p_0;
void* __imp_GetProcAddress = (void*)(uintptr_t)&stub_p_0;
void* __imp_LoadLibraryA = (void*)(uintptr_t)&stub_p_0;
void* __imp_OutputDebugStringA = (void*)(uintptr_t)&stub_ul_1;
void* __imp_GetLastError = (void*)(uintptr_t)&stub_ul_0;
void* __imp_SetLastError = (void*)(uintptr_t)&stub_v;
void* __imp_GetTickCount = (void*)(uintptr_t)&stub_ul_0;
void* __imp_QueryPerformanceCounter = (void*)(uintptr_t)&stub_l_0;
void* __imp_VirtualAlloc = (void*)(uintptr_t)&stub_p_0;
void* __imp_VirtualFree = (void*)(uintptr_t)&stub_l_0;
void* __imp_ReadFile = (void*)(uintptr_t)&stub_ul_1;
void* __imp_GetFileSize = (void*)(uintptr_t)&stub_l_0;
void* __imp_SetEvent = (void*)(uintptr_t)&stub_l_0;
void* __imp_ResetEvent = (void*)(uintptr_t)&stub_l_0;
void* __imp_InitializeCriticalSection = (void*)(uintptr_t)&stub_v;
void* __imp_EnterCriticalSection = (void*)(uintptr_t)&stub_v;
void* __imp_LeaveCriticalSection = (void*)(uintptr_t)&stub_v;
void* __imp_DeleteCriticalSection = (void*)(uintptr_t)&stub_v;
void* __imp_GetSystemTimeAsFileTime = (void*)(uintptr_t)&stub_v;
void* __imp_IsDebuggerPresent = (void*)(uintptr_t)&stub_ul_0;
} // extern "C"

#endif // NEFU_BARE
