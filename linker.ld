/*
 * Simple linker script for our kernel.
 */

ENTRY(start) /* The entry point of our kernel is the 'start' label */

SECTIONS
{
    . = 1M;
    _kernel_phys_start = .;
    .boot :
    {
        *(.multiboot)
    }

    /* Then, put the executable code. */
    .text ALIGN(4K) :
    {
        _text_start = .;
        *(.text)
        _text_end = .;
    }

    /* Then, read-only data. */
    .rodata ALIGN(4K) :
    {
        _rodata_start = .;
        *(.rodata)
        _rodata_end = .;
    }

    /* Then, initialized data. */
    .data ALIGN(4K) :
    {
        _data_start = .;
        *(.data)
        _data_end = .;
    }

    /* Finally, the uninitialized data (BSS). */
    .bss ALIGN(4K) :
    {
        _bss_start = .;
        *(.bss)
        _bss_end = .;
    }
    _kernel_phys_end = .;
}