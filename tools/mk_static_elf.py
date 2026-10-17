#!/usr/bin/env python3
# mk_static_elf.py - build a minimal statically-linked x86_64 ELF64 that only
# uses the Linux write + exit syscalls (via the `syscall` instruction, 0f 05).
# This is a real, directly-runnable Linux ELF; nefuOS's elfrun loads it and
# emulates its syscalls on the bare-metal backend.
#
# Layout (single PT_LOAD, RX):
#   file 0x000      ELF64 header (64B)
#   file 0x040      program header table (1 entry, 56B)
#   file 0x100      code + message string
#   vaddr base      0x600000  (kept high to stay clear of the kernel heap)
import struct, sys

BASE = 0x7800000          # load vaddr (120 MB: inside 0-128MB WB identity map, above 0xA00000 arena bump)
LOFF = 0x100             # file offset where code begins
MSG = b"Hello from Linux ELF in nefuOS!\n"

# --- tiny encoder for the fixed instruction sequence ---
code = bytearray()
def emit(b): code.extend(b)
# write(1, msg, len)
emit(b"\xB8\x01\x00\x00\x00")                      # mov eax, 1
emit(b"\xBF\x01\x00\x00\x00")                      # mov edi, 1
lea_off = len(code)
emit(b"\x48\x8D\x35\x00\x00\x00\x00")              # lea rsi, [rip+rel32]
emit(b"\xBA" + struct.pack("<I", len(MSG)))               # mov edx, len  (BA imm32, 5B)
emit(b"\x0F\x05")                                  # syscall
emit(b"\xB8\x3C\x00\x00\x00")                      # mov eax, 60 (exit)
emit(b"\x31\xFF")                                  # xor edi, edi
emit(b"\x0F\x05")                                  # syscall
# patch lea displacement: msg sits right after the code
msg_off = LOFF + len(code)
rel32 = (msg_off) - (LOFF + lea_off + 7)           # RIP of lea end + rel32 == msg
struct.pack_into("<i", code, lea_off + 3, rel32)
msg_off = LOFF + len(code)
code += MSG

filesz = len(code)
# align
filesz = (filesz + 0x1FF) & ~0x1FF

# --- ELF64 header ---
hdr = bytearray(64)
hdr[0:4] = b"\x7fELF"
hdr[4] = 2                       # ELFCLASS64
hdr[5] = 1                       # ELFDATA2LSB
hdr[6] = 1                       # EV_CURRENT
hdr[7] = 0                       # ELFOSABI_SYSV
struct.pack_into("<H", hdr, 16, 2)       # e_type = ET_EXEC
struct.pack_into("<H", hdr, 18, 62)      # e_machine = EM_X86_64
struct.pack_into("<I", hdr, 20, 1)       # e_version
struct.pack_into("<Q", hdr, 24, BASE)   # e_entry: code is loaded at vaddr==BASE (p_vaddr==BASE)
struct.pack_into("<Q", hdr, 32, 64)      # e_phoff
struct.pack_into("<Q", hdr, 40, 0)       # e_shoff
struct.pack_into("<I", hdr, 48, 64)      # e_ehsize
struct.pack_into("<H", hdr, 52, 56)      # e_phentsize
struct.pack_into("<H", hdr, 54, 1)       # e_phnum
struct.pack_into("<H", hdr, 56, 64)      # e_shentsize
struct.pack_into("<H", hdr, 58, 0)       # e_shnum
struct.pack_into("<H", hdr, 60, 0)       # e_shstrndx

# --- program header: one PT_LOAD (RX) ---
ph = bytearray(56)
struct.pack_into("<I", ph, 0, 1)         # p_type = PT_LOAD
struct.pack_into("<I", ph, 4, 5)         # p_flags = R|X
struct.pack_into("<Q", ph, 8, LOFF)      # p_offset
struct.pack_into("<Q", ph, 16, BASE)     # p_vaddr
struct.pack_into("<Q", ph, 24, BASE)     # p_paddr
struct.pack_into("<Q", ph, 32, filesz)   # p_filesz
struct.pack_into("<Q", ph, 40, filesz)   # p_memsz
struct.pack_into("<Q", ph, 48, 0x1000)   # p_align

out = bytes(hdr) + bytes(ph)
out += b"\x00" * (LOFF - len(out))
out += bytes(code)
out += b"\x00" * (filesz - len(code))
out += b"\x00" * (0x200 - (len(out) & 0x1FF))

with open(sys.argv[1] if len(sys.argv) > 1 else "build/samples/hello-linux-elf",
          "wb") as f:
    f.write(out)
print("wrote %s  (%d bytes, entry=0x%x, msg@%dB)" %
      (sys.argv[1] if len(sys.argv) > 1 else "build/samples/hello-linux-elf",
       len(out), BASE + LOFF, msg_off))
