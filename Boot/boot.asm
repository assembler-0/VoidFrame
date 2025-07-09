section .multiboot
header_start:
    dd 0xE85250D6                ; Multiboot2 magic number
    dd 0                         ; Architecture 0 (protected mode i386)
    dd header_end - header_start ; header length
    dd 0x17ADAF12 ; checksum (calculated: -(0xE85250D6 + 0 + 24))
    ; end tag
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:

bits 32

section .text

; Macro for debugging output
%macro debug_print 1
    mov dx, 0xE9
    mov al, %1
    out dx, al
%endmacro

; Enhanced GDT for long mode (keeping your original structure)
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
    debug_print '1'
    cli
    cld

    ; Save multiboot info for kernel
    mov [multiboot_magic], eax
    mov [multiboot_info], ebx
    debug_print 'B'

    ; Setup temporary stack
    mov esp, stack_top

    ; Check CPU capabilities (optional, won't halt if missing)
    call check_and_enable_features

    ; Load GDT (same as original)
    lgdt [gdt64.pointer]
    debug_print '2'

    ; Enable PAE (same as original)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    debug_print '3'

    ; Setup paging (keeping your original mapping approach)
    mov edi, pml4_table
    mov cr3, edi
    debug_print '4'

    ; Initialize the tables by zeroing them out (same as original)
    mov edi, pml4_table
    mov ecx, (4096 * 3) / 4 ; Zero out PML4, PDP, and PD tables.
    xor eax, eax
    rep stosd

    ; Map the first 16MB of memory (expanded from your 8MB)
    ; pml4_table[0] points to pdp_table
    mov edi, pml4_table
    lea eax, [pdp_table + 0x3] ; Present, R/W
    mov [edi], eax

    ; pdp_table[0] points to pd_table
    mov edi, pdp_table
    lea eax, [pd_table + 0x3] ; Present, R/W
    mov [edi], eax

    ; Map 16MB using 2MB pages (your original approach but more memory)
    mov edi, pd_table
    mov eax, 0x83       ; Present, R/W, 2MB page
    mov ecx, 8          ; 8 * 2MB = 16MB

.map_loop:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000   ; Next 2MB
    loop .map_loop
    debug_print '5'

    ; Enable long mode (same as original)
    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 8      ; LME (Long Mode Enable)
    wrmsr
    debug_print '6'

    ; Enable paging (same as original)
    mov eax, cr0
    or eax, 1 << 31     ; PG (Paging)
    mov cr0, eax
    debug_print '7'

    ; Jump to long mode (same as original)
    debug_print '8'
    jmp 0x08:long_mode

; Check and enable CPU features (non-critical)
check_and_enable_features:
    ; Test for CPUID
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz .no_cpuid

    ; Enable SSE if available
    mov eax, 1
    cpuid
    test edx, 1 << 25   ; SSE
    jz .no_sse

    ; Enable SSE
    mov eax, cr4
    or eax, 1 << 9      ; OSFXSR
    or eax, 1 << 10     ; OSXMMEXCPT
    mov cr4, eax

    mov eax, cr0
    and eax, ~(1 << 2)  ; Clear EM
    or eax, 1 << 1      ; Set MP
    mov cr0, eax

.no_sse:
.no_cpuid:
    ret

bits 64

extern KernelMain

long_mode:
    debug_print '9'
    ; Load data segments (same as original)
    mov ax, 0x10 ; selector for data segment
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Setup proper 64-bit stack
    mov rsp, stack_top

    ; Clear direction flag
    cld


    ; Pass multiboot info to kernel
    mov rdi, [multiboot_magic]
    mov rsi, [multiboot_info]

    debug_print 'A'
    ; Jump to the C kernel (same as original)
    call KernelMain

    ; If kernel returns, halt
    cli
.halt_loop:
    hlt
    jmp .halt_loop

section .data
multiboot_magic: dd 0
multiboot_info: dd 0

section .bss

align 4096
pml4_table: resb 4096
pdp_table:  resb 4096
pd_table:   resb 4096

; Larger stack for better stability
align 16
stack_bottom: resb 8192  ; 8KB stack
stack_top: