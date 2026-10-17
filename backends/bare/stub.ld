/* nefuOS boot stub linker script — merge ALL sections into one flat .text
   so objcopy -O binary -j .text captures code AND the C static const tables
   (length_base / dist_base / order etc. live in .rdata).
   VMA starts at 0x680000: the ping-pong loader runs window 1 in place at
   0x20000, then relocates the whole payload to the 0x680000 staging area
   (which sits ABOVE the kernel image so the inflate destination
   0x100000..0x100000+kSize never overlaps the compressed source).
   Inflate's absolute references to its const tables are therefore emitted
   against 0x680000 and become correct once window 1 has been copied there.
   Must stay in sync with stub.s STAGE_BASE. (stub.s itself is position-
   independent and only uses numeric slot offsets.) */
SECTIONS
{
    . = 0x680000;
    .text : {
        *(.text*)
        *(.rdata*)
        *(.rodata*)
        *(.data*)
    }
}
