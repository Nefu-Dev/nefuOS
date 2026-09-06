/* nefuOS 内核链接脚本 — 平面二进制，加载基址 0x20000 */
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
        *(.bss*)
        *(COMMON)
    }
}
