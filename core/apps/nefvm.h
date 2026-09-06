// nefuOS NEFVM: tiny stack-based VM that executes .bin applications.
// A .bin file is a real binary app package (not a text manifest like .nefud):
// [0..7] magic "NEFBIN01"
// [8..31] bound app name (24 bytes, NUL padded; empty = run bytecode)
// [32..63] description (32 bytes, NUL padded)
// [64..] bytecode executed by the VM
// Opcodes (1 byte, immediates follow in little endian):
// 0x00 HALT 0x01 PUSH8 0x02 PUSH16 0x03 POP 0x04 ADD 0x05 SUB
// 0x06 MUL 0x07 DIV 0x08 PRINT 0x09 PRINTCHR 0x0A PRINTN
// 0x0B JMP16 0x0C JZ16 0x0D JNZ16 0x0E DUP 0x0F SWAP
#pragma once
#include <stdint.h>

namespace nefu {

// Runs the bytecode of a .bin package. out() receives each printed chunk;
// ud is passed through. Returns 0 on success, negative on error.
int nefvm_run(const uint8_t* code, uint32_t len, void (*out)(const char*, void*), void* ud);

} // namespace nefu
