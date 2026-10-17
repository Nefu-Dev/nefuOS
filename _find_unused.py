#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find unused static functions in nefuOS core/backends source."""
import os, re, sys, collections

ROOT = r"D:\mycppos1\nefuOS"
SKIP_DIRS = {"third_party", "build", "dist", "iso", "verify_out", ".git", "a", "tools\\qemu", "tools\\grub_toolchain", "_sweep_out"}

def iter_files():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel = os.path.relpath(dirpath, ROOT)
        parts = rel.split(os.sep)
        if any(p in SKIP_DIRS for p in parts):
            dirnames[:] = []
            continue
        for fn in filenames:
            if fn.endswith((".cpp", ".c", ".h")):
                yield os.path.join(dirpath, fn)

# match:  `static ... name(` at top level (heuristic: function definition start)
# name must be identifier; skip control keywords
FUNC_RE = re.compile(r'^\s*static\s+(?:inline\s+)?(?:[A-Za-z_][\w:<>,*\s&]*?\s+|\w[\w\s]*\s+)([A-Za-z_]\w*)\s*\(', re.M)

# also match definitions that start a line like  `Type name(` without static
DEF_RE = re.compile(r'^\s*(?:static\s+)?(?:inline\s+)?(?:[A-Za-z_][\w:*<>,\s]*?\s+)([A-Za-z_]\w*)\s*\([^;]*$', re.M)

STOP = {"if","for","while","switch","return","sizeof","case","catch","new","delete",
        "assert","printf","sprintf","snprintf","ksprintf","klogf","strlen","strcmp",
        "memcpy","memset","malloc","free","kalloc","kfree","fopen","fclose","fread",
        "fwrite","fprintf","sscanf","strstr","strchr","strncpy","strcpy","strcat",
        "tolower","toupper","abs","min","max","sizeof"}

def analyze(scope="static"):
    counts = collections.defaultdict(list)  # name -> [(file, line)]
    files = list(iter_files())
    print(f"scanning {len(files)} files ...", flush=True)
    for path in files:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except Exception:
            continue
        # strip comments
        text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
        text = re.sub(r'//[^\n]*', '', text)
        if scope == "static":
            for m in FUNC_RE.finditer(text):
                name = m.group(1)
                if name in STOP:
                    continue
                # find the line number
                line = text.count("\n", 0, m.start()) + 1
                counts[name].append((os.path.relpath(path, ROOT), line))
        else:
            for m in DEF_RE.finditer(text):
                name = m.group(1)
                if name in STOP:
                    continue
                line = text.count("\n", 0, m.start()) + 1
                counts[name].append((os.path.relpath(path, ROOT), line))
    return counts

def count_refs(name, files_text):
    n = 0
    for t in files_text:
        n += len(re.findall(r'\b' + re.escape(name) + r'\b', t))
    return n

def main():
    scope = sys.argv[1] if len(sys.argv) > 1 else "static"
    counts = analyze(scope)
    files_text = []
    for path in iter_files():
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                files_text.append(f.read())
        except Exception:
            pass
    unused = []
    for name, locs in counts.items():
        if len(locs) > 1:
            continue  # defined more than once (overload) -> skip
        total = count_refs(name, files_text)
        if total <= 1:  # only the definition
            unused.append((name, locs[0][0], locs[0][1]))
    unused.sort(key=lambda x: (x[1], x[2]))
    print(f"\n=== UNUSED {scope} FUNCTIONS: {len(unused)} ===")
    for name, f, l in unused:
        print(f"{f}:{l}\t{name}")

if __name__ == "__main__":
    main()
