// nefuOS NEFVM implementation (bare-metal safe: no floats, bounded steps)
#include "nefvm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

enum {
    OP_HALT = 0x00, OP_PUSH8 = 0x01, OP_PUSH16 = 0x02, OP_POP = 0x03,
    OP_ADD = 0x04, OP_SUB = 0x05, OP_MUL = 0x06, OP_DIV = 0x07,
    OP_PRINT = 0x08, OP_PRINTCHR = 0x09, OP_PRINTN = 0x0A,
    OP_JMP16 = 0x0B, OP_JZ16 = 0x0C, OP_JNZ16 = 0x0D, OP_DUP = 0x0E, OP_SWAP = 0x0F
};

static const uint32_t VM_MAX_STEPS = 200000u;
static const int VM_STACK_SIZE = 256;

int nefvm_run(const uint8_t* code, uint32_t len, void (*out)(const char*, void*), void* ud) {
    if (!code || len < 64) return -1;
    // validate magic: "NEFBIN01"
    static const char M[8] = { 'N','E','F','B','I','N','0','1' };
    for (int i = 0; i < 8; i++) if (code[i] != (uint8_t)M[i]) return -1;
    int stack[VM_STACK_SIZE];
    int sp = 0;
    uint32_t pc = 64;
    uint32_t steps = 0;
    char buf[32];
    while (pc < len && steps < VM_MAX_STEPS) {
        steps++;
        uint8_t op = code[pc++];
        switch (op) {
        case OP_HALT:
            return 0;
        case OP_PUSH8:
            if (pc < len && sp < VM_STACK_SIZE) stack[sp++] = (int8_t)code[pc++];
            break;
        case OP_PUSH16:
            if (pc + 2 <= len && sp < VM_STACK_SIZE) {
                stack[sp++] = (int16_t)(code[pc] | (code[pc + 1] << 8));
                pc += 2;
            }
            break;
        case OP_POP:
            if (sp > 0) sp--;
            break;
        case OP_ADD:
            if (sp >= 2) { stack[sp - 2] += stack[sp - 1]; sp--; }
            break;
        case OP_SUB:
            if (sp >= 2) { stack[sp - 2] -= stack[sp - 1]; sp--; }
            break;
        case OP_MUL:
            if (sp >= 2) { stack[sp - 2] *= stack[sp - 1]; sp--; }
            break;
        case OP_DIV:
            if (sp >= 2) { stack[sp - 2] = stack[sp - 1] ? stack[sp - 2] / stack[sp - 1] : 0; sp--; }
            break;
        case OP_PRINT:
            if (sp > 0) {
                int v = stack[--sp];
                int n = ksprintf(buf, sizeof(buf), "%d", v);
                if (out && n > 0) out(buf, ud);
            }
            break;
        case OP_PRINTCHR:
            if (sp > 0) {
                buf[0] = (char)(stack[--sp] & 0xFF);
                buf[1] = 0;
                if (out) out(buf, ud);
            }
            break;
        case OP_PRINTN:
            if (out) out("\n", ud);
            break;
        case OP_JMP16:
            if (pc + 2 <= len) { pc = (uint16_t)(code[pc] | (code[pc + 1] << 8)); }
            break;
        case OP_JZ16:
            if (pc + 2 <= len) {
                uint16_t t = (uint16_t)(code[pc] | (code[pc + 1] << 8));
                pc += 2;
                if (sp > 0 && stack[sp - 1] == 0) { sp--; pc = t; }
            }
            break;
        case OP_JNZ16:
            if (pc + 2 <= len) {
                uint16_t t = (uint16_t)(code[pc] | (code[pc + 1] << 8));
                pc += 2;
                if (sp > 0 && stack[sp - 1] != 0) { sp--; pc = t; }
            }
            break;
        case OP_DUP:
            if (sp > 0 && sp < VM_STACK_SIZE) { stack[sp] = stack[sp - 1]; sp++; }
            break;
        case OP_SWAP:
            if (sp >= 2) { int t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; }
            break;
        default:
            return -2; // bad opcode
        }
    }
    return steps >= VM_MAX_STEPS ? -3 : 0;
}

} // namespace nefu
