#!/usr/bin/env python3
# mk_x11_elf.py - build a minimal statically-linked x86_64 ELF64 that acts as a
# real Xlib-style X11 client over the wire protocol (socket/connect/write/read
# syscalls only, no libc).  nefuOS's elfrun maps the guest socket onto the
# embedded X server (core/x11), so this ELF can CreateWindow, CreateGC,
# MapWindow, draw (fill rect / polyline / text / image) and then print a marker.
#
# Layout (single PT_LOAD, RX): header @0, phdr @0x40, code @0x100.
import struct, sys

BASE = 0x7800000          # load vaddr
LOFF = 0x100

WIN_ID   = 0x2001000      # resource ids inside the server's base+mask
GC_ID    = 0x2002000

# ---------- tiny machine-code emitter ----------
code = bytearray()
def emit(b): code.extend(b)
def p32(v): return struct.pack("<I", v & 0xFFFFFFFF)
def p16(v): return struct.pack("<H", v & 0xFFFF)

lea_fixes = []   # (byte_index_of_rel32, target_sym)

def lea_rsi(sym):
    # 48 8D 35 rel32 -> lea rsi, [rip+rel32]
    at = len(code)
    emit(b"\x48\x8D\x35\x00\x00\x00\x00")
    lea_fixes.append((at + 3, sym))

def lea_rdi(sym):
    at = len(code)
    emit(b"\x48\x8D\x3D\x00\x00\x00\x00")
    lea_fixes.append((at + 3, sym))

def sys3(eax, edi, esi, edx):
    emit(b"\xB8" + p32(eax))     # mov eax, imm32
    emit(b"\xBF" + p32(edi))     # mov edi, imm32
    emit(b"\xBE" + p32(esi))     # mov esi, imm32
    emit(b"\xBA" + p32(edx))     # mov edx, imm32
    emit(b"\x0F\x05")            # syscall

def sys2(eax, edi, edx):
    emit(b"\xB8" + p32(eax))
    emit(b"\xBF" + p32(edi))
    emit(b"\xBA" + p32(edx))
    emit(b"\x0F\x05")

# one-byte progress marker to fd 1 (COM1 in the boot selftest), to trace where
# the guest stops when it hangs
def dbg(tag):
    emit(b"\xB8" + p32(1))
    emit(b"\xBF" + p32(1))
    lea_rsi(tag)
    emit(b"\xBA" + p32(1))
    emit(b"\x0F\x05")

# ---- socket(1, 1, 0) -> rax = fd ----
emit(b"\xB8" + p32(41))
emit(b"\xBF" + p32(1))           # AF_UNIX
emit(b"\xBE" + p32(1))           # SOCK_STREAM
emit(b"\x31\xD2")                # xor edx, edx
emit(b"\x0F\x05")
emit(b"\x49\x89\xC4")            # mov r12, rax   ; keep fd in r12
dbg("dbg_s")

# ---- connect(fd, &addr, 22) ----
emit(b"\xB8" + p32(42))
emit(b"\x4C\x89\xE7")            # mov rdi, r12
lea_rsi("addr")
emit(b"\xBA" + p32(22))
emit(b"\x0F\x05")
dbg("dbg_c")

# ---- write(fd, &setup, 12) ----
emit(b"\xB8" + p32(1))
emit(b"\x4C\x89\xE7")
lea_rsi("setup")
emit(b"\xBA" + p32(12))
emit(b"\x0F\x05")
dbg("dbg_w")

# ---- read(fd, &reply, 512) until > 0 ----
read_loop_at = len(code)
emit(b"\xB8" + p32(0))
emit(b"\x4C\x89\xE7")
lea_rsi("reply")
emit(b"\xBA" + p32(512))
emit(b"\x0F\x05")
emit(b"\x48\x85\xC0")            # test rax, rax
emit(b"\x7E\xFB")                # jle read_loop (rel8 back to mov)
dbg("dbg_r")

# ---- request batch 1: CreateWindow + CreateGC + MapWindow ----
emit(b"\xB8" + p32(1))
emit(b"\x4C\x89\xE7")
lea_rsi("req1")
emit(b"\xBA" + p32(40 + 28 + 12))   # len of the three requests (80 bytes)
emit(b"\x0F\x05")
dbg("dbg_1")

# ---- read events (Expose) until > 0 ----
read_loop2_at = len(code)
emit(b"\xB8" + p32(0))
emit(b"\x4C\x89\xE7")
lea_rsi("reply")
emit(b"\xBA" + p32(512))
emit(b"\x0F\x05")
emit(b"\x48\x85\xC0")
emit(b"\x7E\xFB")
dbg("dbg_2")

# ---- request batch 2: draw (fill rect + polyline + text + image) ----
emit(b"\xB8" + p32(1))
emit(b"\x4C\x89\xE7")
lea_rsi("req2")
emit(b"\xBA" + p32(28 + 36 + 32 + 220))   # len of the four draw requests (316)
emit(b"\x0F\x05")
dbg("dbg_3")

# ---- write(1, msg, len) ----
emit(b"\xB8" + p32(1))
emit(b"\xBF" + p32(1))
lea_rsi("msg")
emit(b"\xBA" + p32(25))          # len of msg
emit(b"\x0F\x05")

# ---- exit(0) ----
emit(b"\xB8" + p32(60))
emit(b"\x31\xFF")
emit(b"\x0F\x05")
dbg("dbg_e")

# ---------- data ----------
data = bytearray()

def be16(v): return struct.pack(">H", v & 0xFFFF)

def data_add(name, blob, align4=True):
    global data
    if align4:
        while len(data) & 3: data.append(0)
    at = len(data)
    data += blob
    symbols[name] = at
    return at

symbols = {}
# sockaddr_un: family + path
path = b"/tmp/.X11-unix/X0\x00"
addr = struct.pack("<H", 1) + path
addr += b"\x00" * (22 - len(addr))
data_add("addr", addr)
# setup request (12 bytes): byte-order 'l', proto 11.0 (big-endian fields)
setup = b"l\x00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00"
data_add("setup", setup)
# reply buffer (512 zeroed)
data_add("reply", b"\x00" * 512)
# --- req1: CreateWindow + CreateGC + MapWindow (X11 6-byte header: op|attr|len(BE)|seq) ---
createwin = bytearray()
createwin += b"\x01" + bytes([24])              # op=1, depth=24
createwin += be16(10) + p16(1)                  # len=10 dwords, seq
createwin += p32(WIN_ID)                        # window id @6
createwin += p32(0x2000000)                     # parent = root @10
createwin += p16(100) + p16(50)                 # x@14, y@16
createwin += p16(500) + p16(400)                # w@18, h@20
createwin += p16(0)                             # border @22
createwin += p16(1)                             # class = InputOutput @24
createwin += p32(0)                             # visual = CopyFromParent @26
createwin += p32(0x200)                         # mask @30: bit9 = event-mask
createwin += p32(0x8045)                        # value: Exposure|KeyPress|ButtonPress|PointerMotion @34
createwin += b"\x00\x00"                        # pad to 40
creategc = bytearray()
creategc += b"\x37\x00"                         # op=55 (CreateGC)
creategc += be16(7) + p16(1)                    # len=7, seq
creategc += p32(GC_ID)                          # cid @6
creategc += p32(WIN_ID)                         # drawable @10
creategc += p32(0xC)                            # mask @14: fg|bg
creategc += p32(0xFFFFFF)                       # fg = white @18
creategc += p32(0x000000)                       # bg = black @22
creategc += b"\x00\x00"                         # pad to 28
mapwin = bytearray()
mapwin += b"\x08\x00"                           # op=8 (MapWindow)
mapwin += be16(3) + p16(1)
mapwin += p32(WIN_ID)                           # wid @6
mapwin += b"\x00\x00"                           # pad to 12 (len=3 dwords)
data_add("req1", bytes(createwin) + bytes(creategc) + bytes(mapwin))
# --- req2: PolyFillRectangle + PolyLine + ImageText8 + PutImage ---
fillrect = bytearray()
fillrect += b"\x46\x00"                         # op=70
fillrect += be16(7) + p16(1)                    # len=7 (28B)
fillrect += p32(WIN_ID) + p32(GC_ID)            # d@6, g@10
fillrect += p16(1) + b"\x00\x00"                # n@14, pad@16
fillrect += p16(20) + p16(20) + p16(200) + p16(150)   # rect @18
fillrect += b"\x00\x00"                         # pad to 28
polyline = bytearray()
polyline += b"\x42\x00"                         # op=66
polyline += be16(9) + p16(1)                    # len=9 (36B)
polyline += p32(WIN_ID) + p32(GC_ID)            # d@6, g@10
polyline += b"\x00" + b"\x00"                   # coord mode @14, pad @15
polyline += p16(4)                              # n @16
for (px, py) in [(250, 180), (350, 60), (450, 180), (250, 180)]:
    polyline += p16(px) + p16(py)               # points @18
polyline += b"\x00\x00"                         # pad to 36
imagetext = bytearray()
imagetext += b"\x4C\x00"                         # op=76 ImageText8
imagetext += be16(8) + p16(1)                    # len=8 (32B)
imagetext += p32(WIN_ID) + p32(GC_ID)            # d@6, g@10
imagetext += p16(30) + p16(220)                 # x@14, y@16
imagetext += bytes([10]) + b"\x00\x00\x00"      # len@18, pad@19-21
imagetext += b"nefuOS X11"                      # text @22
putimage = bytearray()
putimage += b"\x48\x00"                         # op=72 PutImage
putimage += be16(55) + p16(1)                   # len=55 dwords (220B total)
putimage += p32(WIN_ID) + p32(GC_ID)            # d@6, g@10
putimage += p16(8) + p16(8)                     # w@14, h@16
putimage += p16(30) + p16(260)                  # dst x@18, y@20
putimage += b"\x00"                             # left_pad @22
putimage += bytes([24])                         # depth @23
putimage += p16(2)                              # format ZPixmap @24
img = bytearray()
for yy in range(8):
    for xx in range(8):
        img += bytes([(255 if xx < 4 else 0), (255 if yy < 4 else 0), (xx * 32)])  # RGB
putimage += bytes(img)                          # data @26 (192 bytes)
putimage += b"\x00\x00"                         # pad to 220
data_add("req2", bytes(fillrect) + bytes(polyline) + bytes(imagetext) + bytes(putimage))
# msg
data_add("msg", b"[X11REQ] x11 client drew ok\n", align4=False)
# progress markers (1 byte each, written to fd 1 to trace guest progress)
for _t in ("s", "c", "w", "r", "1", "2", "3", "e"):
    data_add("dbg_" + _t, bytes([ord(_t)]), align4=False)

# ---------- patch lea rel32s ----------
# Guest mapping: file offset (LOFF + payload offset) maps to guest vaddr
# BASE + payload offset; payload starts at file LOFF with code first, then
# data.  So data symbol vaddr (relative to BASE) = len(code) + symbols[sym].
def sym_off(sym): return len(code) + symbols[sym]
for (rel_at, sym) in lea_fixes:
    # lea ends at rel_at+4 (x86 rel32 is relative to the next instruction);
    # target = sym (guest vaddr relative to BASE).  Both sides are vaddr
    # space: no LOFF term here (file offsets were folded into sym_off via
    # len(code)+symbols[sym]).
    rel = sym_off(sym) - (rel_at + 4)
    struct.pack_into("<i", code, rel_at, rel)

# Segment file size = actual payload bytes at p_offset; segment mem size is
# page-rounded (elfrun requires p_offset + p_filesz <= total file size).
seg_fsz = len(code) + len(data)
seg_msz = (seg_fsz + 0x1FF) & ~0x1FF

# ---------- ELF64 header ----------
hdr = bytearray(64)
hdr[0:4] = b"\x7fELF"
hdr[4] = 2; hdr[5] = 1; hdr[6] = 1; hdr[7] = 0
struct.pack_into("<H", hdr, 16, 2)
struct.pack_into("<H", hdr, 18, 62)
struct.pack_into("<I", hdr, 20, 1)
struct.pack_into("<Q", hdr, 24, BASE)  # entry: p_offset 0x100 maps to vaddr BASE
struct.pack_into("<Q", hdr, 32, 64)
struct.pack_into("<I", hdr, 48, 64)
struct.pack_into("<H", hdr, 52, 56)
struct.pack_into("<H", hdr, 54, 1)
struct.pack_into("<H", hdr, 56, 64)
# phdr
ph = bytearray(56)
struct.pack_into("<I", ph, 0, 1)
struct.pack_into("<I", ph, 4, 5)
struct.pack_into("<Q", ph, 8, LOFF)
struct.pack_into("<Q", ph, 16, BASE)
struct.pack_into("<Q", ph, 24, BASE)
struct.pack_into("<Q", ph, 32, seg_fsz)
struct.pack_into("<Q", ph, 40, seg_msz)
struct.pack_into("<Q", ph, 48, 0x1000)

out = bytes(hdr) + bytes(ph)
out += b"\x00" * (LOFF - len(out))
out += bytes(code)
out += bytes(data)
out += b"\x00" * ((LOFF + seg_msz) - len(out))

outp = sys.argv[1] if len(sys.argv) > 1 else "build/samples/x11-hello-elf"
with open(outp, "wb") as f:
    f.write(out)

# also emit the C header for the boot selftest
import os
cpath = os.path.join(os.path.dirname(outp), "x11_selfcheck_bin.h")
if "--header" in sys.argv or True:
    cpath = os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "core", "apps", "x11_selfcheck_bin.h"))
    with open(cpath, "w", encoding="utf-8") as f:
        f.write("// core/apps/x11_selfcheck_bin.h - generated by tools/mk_x11_elf.py\n")
        f.write("// Minimal static Linux ELF: X11 wire-protocol client (socket/connect/write/read).\n")
        f.write("#pragma once\n")
        f.write("#include <stdint.h>\n")
        f.write("static const unsigned int NEFU_X11_SELFCHECK_LEN = %d;\n" % len(out))
        f.write("static const uint8_t NEFU_X11_SELFCHECK_BIN[%d] = {\n" % len(out))
        for i in range(0, len(out), 16):
            row = out[i:i+16]
            f.write("  " + ",".join("0x%02x" % b for b in row) + ",\n")
        f.write("};\n")
    print("wrote header %s (%d bytes)" % (cpath, len(out)))

print("wrote %s  (%d bytes, entry=0x%x)" % (outp, len(out), BASE))
