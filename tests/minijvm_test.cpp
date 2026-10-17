// minijvm_test.cpp - headless functional tests for the nefuOS mini JVM
// (core/apps/minijvm.cpp). Compiles real Java with javac, embeds the .class
// bytes (tests/minijvm_cases.h) and verifies the interpreter output against
// the expected lines recorded in tests/java/*.expect.
//
// Build: tests/build_minijvm_test.ps1   (host-only, needs a JDK on PATH)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../core/apps/minijvm.h"
#include "minijvm_cases.h"

using namespace nefu;

static std::string s_out;

static void sink(const char* s, void* ud) {
    (void)ud;
    if (s) s_out += s;
}

int main() {
    printf("== minijvm headless test ==\n");
    int fails = 0, checks = 0;
    for (int i = 0; i < k_java_case_count; i++) {
        const JavaCase& c = k_java_cases[i];
        s_out.clear();
        int rc = minijvm_run_class(c.data, c.size, sink, 0);
        checks++;
        if (rc != 0) {
            fails++;
            printf("  FAIL: %s returned %d (expected 0)\n", c.name, rc);
            printf("        stderr: %s\n", s_out.c_str());
            continue;
        }
        std::string expect(c.expect);
        // the generator stores newlines as literal backslash-n in a C string;
        // decode them back
        std::string want;
        for (size_t k = 0; k < expect.size(); k++) {
            if (expect[k] == '\\' && k + 1 < expect.size() && expect[k + 1] == 'n') {
                want += '\n';
                k++;
            } else {
                want += expect[k];
            }
        }
        checks++;
        if (s_out != want) {
            fails++;
            printf("  FAIL: %s output mismatch\n", c.name);
            printf("        got:   %s", s_out.c_str());
            printf("        want:  %s", want.c_str());
        } else {
            printf("  ok:   %s (%u bytes)\n", c.name, c.size);
        }
    }
    printf("checks=%d fails=%d\n", checks, fails);
    return fails ? 1 : 0;
}
