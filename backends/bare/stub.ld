/* nefuOS boot stub linker script — merge ALL sections into one flat .text
   so objcopy -O binary -j .text captures code AND the C static const tables
   (length_base / dist_base / order etc. live in .rdata).
   VMA starts at 0x20000: the stub is loaded at physical 0x20000 by boot.s,
   so every absolute reference (inflate's table addresses AND stub.s's slot
   addresses 0x20080/84/88) matches the runtime physical address. */
SECTIONS
{
    . = 0x20000;
    .text : {
        *(.text*)
        *(.rdata*)
        *(.rodata*)
        *(.data*)
    }
}
