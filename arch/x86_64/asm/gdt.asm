gdt64:
    ; Null Descriptor
    dq 0
.code: equ $ - gdt64 ; offset 0x08
    dw 0xFFFF      ; limit
    dw 0           ; base
    db 0           ; base
    db 10011010b   ; 0x9A - Present, DPL 0, Code, Executable, Read/Write
    db 10101111b   ; 0xAF - Granularity (4K), 64-bit Code (L-bit=1), Limit
    db 0           ; base
.data: equ $ - gdt64 ; offset 0x10
    dw 0xFFFF      ; limit
    dw 0           ; base
    db 0           ; base
    db 10010010b   ; 0x92 - Present, DPL 0, Data, Read/Write
    db 11001111b   ; 0xCF - Granularity (4K), 32-bit Segment, Limit
    db 0           ; base
gdt_end:

gdt64_pointer:
    dw gdt_end - gdt64 - 1 ; GDT size
    dq gdt64               ; GDT base address