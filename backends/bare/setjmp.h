/* setjmp.h — freestanding x86-64 setjmp/longjmp shim for the nefuOS kernel.
   MicroPython's NLR (MICROPY_NLR_SETJMP) and GC regs capture (gchelper_generic)
   need setjmp/longjmp, which the hosted CRT does not provide in a freestanding
   kernel.  The implementation lives in setjmp.S and saves the callee-saved
   integer registers, the stack pointer and the return address. */
#ifndef NEFU_BARE_SETJMP_H
#define NEFU_BARE_SETJMP_H

/* jmp_buf[0..7] = rbx, rbp, r12, r13, r14, r15, rsp, rip */
typedef unsigned long long jmp_buf[8];

#ifdef __cplusplus
extern "C" {
#endif

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#ifdef __cplusplus
}
#endif

#endif /* NEFU_BARE_SETJMP_H */
