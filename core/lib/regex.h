// nefuOS regex library — small backtracking regex engine
// Portable: no STL, no exceptions. Supports:
//   literals, '.', classes [abc] [a-z] [^...], escapes \d \D \w \W \s \S \t \n \r
//   quantifiers * + ? {n} {n,} {n,m} (greedy), anchors ^ $, groups (...) with
//   capture (max 9), alternation |.
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace regex {

struct Match {
    int start;   // byte offset of match start (-1 = no match)
    int len;     // byte length of match
    int groups[10][2]; // capture group start/len per group index (0 = whole)
    Match() { reset(); }
    void reset() {
        start = -1; len = 0;
        for (int i = 0; i < 10; i++) { groups[i][0] = -1; groups[i][1] = 0; }
    }
};

// Compile the pattern. Returns false + err message on syntax error.
bool compile(const char* pattern, const char** err = 0);

// Reset captured groups (call before each search if reusing).
void reset_state();

// Find first match of the compiled pattern in text.
bool search(const char* text, Match& m);

// True if the pattern matches the whole string.
bool full_match(const char* text);

// True if the pattern matches any substring.
bool contains(const char* text);

// Simple one-shot helpers (compile internally).
bool match_anywhere(const char* pattern, const char* text, Match* m = 0);
bool match_full(const char* pattern, const char* text);

} // namespace regex
} // namespace nefu
