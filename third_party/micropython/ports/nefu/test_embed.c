#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include "nefu_mp.h"
#include "py/frozenmod.h"

// Stub for the isolated test: modnefu_http.c bridges to the host HTTPS stack
// (win32.cpp nefu_http_request), which is not linked into this test binary.
int nefu_http_request(const char* method, const char* url,
                      const char* body, const char* content_type,
                      const char* extra_headers,
                      int* out_status, char** out_headers, uint32_t* out_headers_len,
                      char** out_body, uint32_t* out_body_len) {
    (void)method; (void)url; (void)body; (void)content_type; (void)extra_headers;
    (void)out_status; (void)out_headers; (void)out_headers_len; (void)out_body; (void)out_body_len;
    return -1;   // transport unavailable in the isolated harness
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    fprintf(stderr, "\n[CRASH] code=0x%lx addr=%p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, NULL, TRUE);
    STACKFRAME64 sf;
    memset(&sf, 0, sizeof(sf));
    sf.AddrPC.Offset = ep->ContextRecord->Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ep->ContextRecord->Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ep->ContextRecord->Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    for (int i = 0; i < 24; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf,
                         ep->ContextRecord, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL)) {
            break;
        }
        if (sf.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 disp = 0;
        char *symbuf = (char *)malloc(sizeof(SYMBOL_INFO) + 255 * sizeof(char));
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym)) {
            fprintf(stderr, "  #%-2d 0x%llx %s+0x%llx\n", i,
                    (unsigned long long)sf.AddrPC.Offset, sym->Name,
                    (unsigned long long)disp);
        } else {
            fprintf(stderr, "  #%-2d 0x%llx\n", i,
                    (unsigned long long)sf.AddrPC.Offset);
        }
        free(symbuf);
    }
    fflush(stderr);
    ExitProcess(0xDead);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void out(const char *s, int n) {
    if (n > 0) fwrite(s, 1, (size_t)n, stdout);
    fflush(stdout);
}

int main(void) {
    SetUnhandledExceptionFilter(crash_filter);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("before init\n");
    nefu_mp_init();
    printf("after init\n");
    nefu_mp_set_output(out);
    printf("version = %s\n", nefu_mp_version());
    printf("run 1 (simple expr)\n");
    nefu_mp_exec("1 + 1\n", 0);
    printf("run 2 (print)\n");
    nefu_mp_exec("print('a', 2 + 2)\n", 0);
    printf("run 3 (for loop)\n");
    nefu_mp_exec("x = 0\nfor i in range(3):\n    x = x + i\nprint('x =', x)\n", 1);
    printf("run 4 (import math)\n");
    nefu_mp_exec("import math\nprint('sqrt =', math.sqrt(144))\n", 1);
    printf("run 5 (generator)\n");
    nefu_mp_exec("print(list(x * x for x in range(6)))\n", 0);
    printf("run 6 (exception)\n");
    nefu_mp_exec("raise ValueError('boom')\n", 1);
    printf("run 7 (recursion)\n");
    nefu_mp_exec("def f(n):\n    return 1 if n <= 0 else n * f(n - 1)\nprint('5! =', f(5))\n", 1);
    printf("run 8 (json)\n");
    nefu_mp_exec("import json\nprint('json =', json.dumps({'a': [1, 2, 3], 'b': 'x'}))\n", 1);
    printf("run 9 (random)\n");
    nefu_mp_exec("import random\nprint('random =', 0 <= random.randint(1, 10) <= 10)\n", 1);
    printf("run 10 (binascii)\n");
    nefu_mp_exec("import binascii\nprint('hex =', binascii.hexlify(b'abc'))\n", 1);
    printf("run 11 (heapq)\n");
    nefu_mp_exec("import heapq\nh = [3, 1, 2]\nheapq.heapify(h)\nprint('heap =', heapq.heappop(h))\n", 1);
    printf("run 12 (struct)\n");
    nefu_mp_exec("import struct\nprint('struct =', struct.unpack('<H', struct.pack('<H', 0x1234))[0])\n", 1);
    printf("run 13 (collections deque)\n");
    nefu_mp_exec("import collections\nd = collections.deque([1, 2, 3], 5)\nd.append(4)\nprint('deque =', list(d))\n", 1);
    printf("run 14 (array)\n");
    nefu_mp_exec("import array\nprint('array =', array.array('i', [1, 2, 3])[1])\n", 1);
    printf("run 15 (asyncio)\n");
    {
        int ft = -1;
        void *md = NULL;
        int fr = mp_find_frozen_module("asyncio", &ft, &md);
        printf("frozen-probe r=%d type=%d\n", fr, ft);
    }
    nefu_mp_exec("import sys\nprint('syspath=', sys.path)\nimport asyncio\nprint('asyncio ok')\n", 1);
    printf("run 16 (uctypes)\n");
    nefu_mp_exec("import uctypes\nprint('uctypes ok')\n", 1);
    printf("run 17 (hashlib sha256)\n");
    nefu_mp_exec("import hashlib\nprint('sha =', hashlib.sha256(b'abc').digest().hex())\n", 1);
    printf("done\n");
    nefu_mp_deinit();
    printf("after deinit\n");
    return 0;
}
