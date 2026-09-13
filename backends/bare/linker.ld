/* nefuOS kernel linker script - flat binary, load base 0x20000 */
/* Note: mingw ld (PE output) recomputes section RVAs itself; keep the
   layout fully relative (no explicit . =) so the __bss_start/__bss_end
   symbols stay as absolute VMAs the entry.s zero-fill can use. */
ENTRY(kernel_start)

SECTIONS
{
    . = 0x20000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
}
