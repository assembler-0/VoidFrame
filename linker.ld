/*
 * linker.ld
 */

ENTRY(start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS {
    . = 0x100000;

    .text : {
        *(.multiboot)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
