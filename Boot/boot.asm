section .multiboot_header
header_start:
    dd 0xE85250D6                ; Multiboot2 magic number
    dd 0                         ; Architecture 0 (protected mode i386)
    dd header_end - header_start ; header length
    dd -(0xE85250D6 + 0 + (header_end - header_start)) ; checksum
    ; end tag
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:

bits 32

section .text

; GDT for long mode
gdt64:
    dq 0 ; NULL descriptor
.code: ; offset 0x08
    dw 0
    dw 0
    db 0
    db 10011010b ; 0x9A - Code Segment, Present, DPL 0, Exec, Read/Write
    db 00100000b ; 0x20 - 64-bit Code (L-bit=1)
    db 0
.data: ; offset 0x10
    dw 0
    dw 0
    db 0
    db 10010010b ; 0x92 - Data Segment, Present, DPL 0, Read/Write
    db 00000000b ; 0x00
    db 0
.pointer:
    dw .pointer - gdt64 - 1
    dq gdt64

global start
start:
    cli
    ; load the GDT
    lgdt [gdt64.pointer]

    ; enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; setup paging
    ; point CR3 to the PML4 table
    mov edi, pml4_table
    mov cr3, edi

    ; initialize the tables by zeroing them out
    mov edi, pml4_table
    mov ecx, (4096 * 3) / 4 ; Zero out PML4, PDP, and PD tables.
    xor eax, eax
    rep stosd

    ; map the first 2MB of memory
    ; pml4_table[0] points to pdp_table
    mov edi, pml4_table
    lea eax, [pdp_table + 0x3] ; Present, R/W
    mov [edi], eax

    ; pdp_table[0] points to pd_table
    mov edi, pdp_table
    lea eax, [pd_table + 0x3] ; Present, R/W
    mov [edi], eax

    ; pd_table[0] maps the first 2MB
    mov edi, pd_table
    mov dword [edi], 0x83 ; Present, R/W, 2MB page

    ; enable long mode
    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 8     ; LME (Long Mode Enable)
    wrmsr

    ; enable paging
    mov eax, cr0
    or eax, 1 << 31    ; PG (Paging)
    mov cr0, eax

    jmp 0x08:long_mode

bits 64

extern KernelMain

long_mode:
    ; load data segments
    mov ax, 0x10 ; selector for data segment
    mov ss, ax
    mov ds, ax
    mov es, ax

    ; jump to the C kernel
    call KernelMain

    hlt

section .bss

align 4096
pml4_table: resb 4096
pdp_table:  resb 4096
pd_table:   resb 4096
