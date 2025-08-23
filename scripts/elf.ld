ENTRY(_start)

SECTIONS {
    . = 0xFFFFFFFF80800000;  /* Kernel base + 8MB */

    .text ALIGN(4K) : {
        *(.text.entry)  /* Entry point first */
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
        *(.rodata.str*)
    }

    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
        *(.got*)
        *(.toc*)
    }

    .bss ALIGN(4K) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Optional: Add symbols for runtime use */
    __process_start = 0xFFFFFFFF80800000;
    __process_end = .;
    __process_size = __process_end - __process_start;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
        *(.interp)
        *(.dynamic)
    }
}