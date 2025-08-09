section .multiboot
header_start:
    dd 0xE85250D6                ; Multiboot2 magic number
    dd 0                         ; Architecture 0 (protected mode i386)
    dd header_end - header_start ; header length
    ; checksum = -(magic + arch + length)
    dd -(0xE85250D6 + 0 + (header_end - header_start))

    ; End tag - required
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:

bits 32

section .text

; Macro for debugging output on Bochs/QEMU's 0xE9 port
%macro debug_print 1
    mov dx, 0xE9
    mov al, %1
    out dx, al
%endmacro

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

global start
start:
    cli ; Disable interrupts
    cld ; Clear direction flag

    ; GRUB provides the magic in EAX and info pointer in EBX.
    ; If this works, you'll see the correct magic number here.
    mov [multiboot_magic], eax
    mov [multiboot_info], ebx
    debug_print '1' ; Start

    ; Setup temporary stack
    mov esp, stack_top

    ; Check and enable CPU features
    call check_and_enable_features
    debug_print 'F' ; Features Enabled/Checked

    ; Load GDT
    lgdt [gdt64_pointer]
    debug_print '2' ; GDT Loaded

    ; Enable PAE (required for Long Mode and NX bit)
    mov eax, cr4
    or eax, 1 << 5 ; Set PAE bit
    mov cr4, eax
    debug_print '3' ; PAE Enabled

    ; Setup page tables
    ; Zero out the tables first
    mov edi, pml4_table
    mov ecx, 4096 * 6 ; 6 pages to clear (PML4, PDP, PD1, PD2, PD3, PD4)
    xor eax, eax
    rep stosb

    ; Set CR3 to the physical address of the PML4 table.
    ; Because we're identity mapping, the virtual address is the physical address.
    mov eax, pml4_table
    mov cr3, eax
    debug_print '4' ; CR3 Loaded

    ; Map the first 4GB of memory
    ; PML4[0] -> PDP Table
    mov edi, pml4_table
    lea eax, [pdp_table + 3] ; Address of PDP table + Present, R/W flags
    mov [edi], eax

    ; PDP[0] -> PD Table 1 (0-1GB)
    mov edi, pdp_table
    lea eax, [pd_table + 3] ; Address of PD table + Present, R/W flags
    mov [edi], eax
    
    ; PDP[1] -> PD Table 2 (1-2GB)
    lea eax, [pd_table2 + 3]
    mov [edi + 8], eax
    
    ; PDP[2] -> PD Table 3 (2-3GB)
    lea eax, [pd_table3 + 3]
    mov [edi + 16], eax
    
    ; PDP[3] -> PD Table 4 (3-4GB)
    lea eax, [pd_table4 + 3]
    mov [edi + 24], eax

    ; Map 4GB using separate loops for each PD table
    mov edi, pd_table
    mov ecx, 512
    mov eax, 0x83  ; Present, R/W, 2MB page size
.map_pd1:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .map_pd1

    mov edi, pd_table2
    mov ecx, 512
.map_pd2:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .map_pd2

    mov edi, pd_table3
    mov ecx, 512
.map_pd3:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .map_pd3

    mov edi, pd_table4
    mov ecx, 512
.map_pd4:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    loop .map_pd4
    debug_print '5' ; Memory Mapped

    ; Enable Long Mode in EFER MSR
    mov ecx, 0xC0000080 ; EFER MSR address
    rdmsr
    or eax, 1 << 8      ; Set LME (Long Mode Enable) bit
    wrmsr
    debug_print '6' ; Long Mode Enabled

    ; Enable Paging (and protected mode)
    mov eax, cr0
    or eax, 1 << 31     ; Set PG (Paging) bit
    or eax, 1 << 0      ; Set PE (Protected Mode) bit (already set if in protected mode, but good practice)
    mov cr0, eax
    debug_print '7' ; Paging On!

    ; Jump to long mode
    jmp gdt64.code:long_mode

; -----------------------------------------------------------
; check_and_enable_features:
;   Checks for CPUID support and enables common/critical CPU features
;   like SSE/SSE2 and No-Execute (NX) if available.
; -----------------------------------------------------------
check_and_enable_features:
    ; Test for CPUID capability (ID bit in EFLAGS)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21 ; Flip the ID bit
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz .no_cpuid_present ; If ID bit couldn't be flipped, CPUID not supported.

    debug_print 'C' ; CPUID detected

    ; Get basic CPU features (CPUID Leaf 1)
    mov eax, 1
    cpuid

    ; EAX = Version Info (Model, Family, Stepping)
    ; EBX = Brand Index, CLFLUSH size, Number of Logical Processors, APIC ID
    ; ECX = Feature flags 1 (SSE3, SSSE3, SSE4.1, SSE4.2, AVX, etc.)
    ; EDX = Feature flags 2 (FPU, VME, MMX, SSE, SSE2, FXSR, MSR, PAE, APIC, PGE, PSN, CMOV, MTRR, HTT, etc.)

    ; --- Enable FPU/SSE/SSE2/XMM support ---
    ; This is required for modern C/C++ compiled code that uses floating point.
    ; CR0.EM (Emulation) should be 0.
    ; CR0.MP (Monitor Coprocessor) should be 1.
    ; CR4.OSFXSR (Operating System FXSAVE/FXRSTOR Support) should be 1.
    ; CR4.OSXMMEXCPT (Operating System Unmasked SIMD Floating-Point Exception Support) should be 1.

    ; Check for SSE support (EDX bit 25)
    test edx, 1 << 25   ; SSE
    jz .skip_sse_enable

    mov eax, cr0
    and eax, ~(1 << 2)  ; Clear EM (Emulation)
    or eax, 1 << 1      ; Set MP (Monitor Coprocessor)
    mov cr0, eax

    mov eax, cr4
    or eax, 1 << 9      ; Set OSFXSR (Operating System FXSAVE/FXRSTOR Support)
    or eax, 1 << 10     ; Set OSXMMEXCPT (Operating System Unmasked SIMD Floating-Point Exception Support)
    mov cr4, eax
    debug_print 'S' ; SSE Enabled

.skip_sse_enable:

    ; --- Enable Write Protect (WP) in CR0 ---
    ; CR0.WP (bit 16) protects read-only pages from ring 0 writes.
    ; This is crucial for proper memory protection.
    mov eax, cr0
    or eax, 1 << 16     ; Set WP bit
    mov cr0, eax
    debug_print 'W' ; Write Protect Enabled

    ; --- Enable No-Execute (NXE) in EFER MSR ---
    ; This feature helps prevent buffer overflow attacks by marking memory pages as non-executable.
    ; Requires PAE to be enabled.
    ; Check for NX support (EDX bit 20 from CPUID leaf 1)
    mov eax, 1
    cpuid
    test edx, 1 << 20   ; NX (Execute Disable Bit)
    jz .skip_nxe_enable

    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 11     ; Set NXE (No-Execute Enable) bit
    wrmsr
    debug_print 'N' ; NX Enabled

.skip_nxe_enable:

.no_cpuid_present:
    ret

bits 64

extern KernelMain

long_mode:
    debug_print '8' ; Entered 64-bit mode

    ; Load data segment registers
    mov ax, gdt64.data
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Setup the final 64-bit stack
    mov rsp, stack_top

    ; Pass multiboot info to the C kernel
    ; RDI is the first argument in the System V AMD64 ABI
    ; RSI is the second argument
    ; Use RDI/RSI directly, as Multiboot2 info pointer can be > 4GB
    mov edi, [multiboot_magic] ; EAX holds magic, so EDI will get the 32-bit magic
    mov rsi, [multiboot_info]  ; EBX holds info pointer, but could be 64-bit, so use RSI

    debug_print '9' ; Calling Kernel
    ; Jump to the C kernel
    call KernelMain

    ; If the kernel ever returns, hang the system
.halt:
    cli
    hlt
    jmp .halt

section .bss
align 4096
pml4_table: resb 4096
pdp_table:  resb 4096
pd_table:   resb 4096
pd_table2:  resb 4096
pd_table3:  resb 4096
pd_table4:  resb 4096

align 16
stack_bottom: resb 8192  ; 8KB stack
stack_top:

section .data
multiboot_magic: dd 0
multiboot_info:  dq 0 ; The info pointer can be a 64-bit address, use dq