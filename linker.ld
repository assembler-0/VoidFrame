/*
 * Simple linker script for our kernel.
 */

ENTRY(start) /* The entry point of our kernel is the 'start' label */

SECTIONS
{
    . = 1M;

    .boot :
    {
        *(.multiboot)
    }

    /* Then, put the executable code. */
    .text :
    {
        *(.text)
    }

    /* Then, read-only data. */
    .rodata :
    {
        *(.rodata)
    }

    /* Then, initialized data. */
    .data :
    {
        *(.data)
    }

    /* Finally, the uninitialized data (BSS). */
    .bss :
    {
        *(.bss)
    }
}