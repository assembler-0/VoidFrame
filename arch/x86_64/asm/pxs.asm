; ============================================================================
; VoidFrame start code for x86_64 architecture
; ============================================================================

[bits 32]

%include "multiboot2.asm"
%include "bss.asm"

section .text

%include "macros.asm"
%include "gdt.asm"
%include "features.asm"

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
    debug_print 'K'
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

    ; Setup initial 4GB page tables (we'll expand this later in C)
    ; Zero out the tables first
    mov edi, pml4_table
    mov ecx, 4096 * 6 ; 6 pages to clear (PML4, PDP, PD1, PD2, PD3, PD4)
    xor eax, eax
    rep stosb
    debug_print 'Z' ; Page Tables Zeroed

    ; Set CR3 to the physical address of the PML4 table.
    ; Because we're identity mapping, the virtual address is the physical address.
    mov eax, pml4_table
    mov cr3, eax
    debug_print '4' ; CR3 Loaded

    ; Map the first 4GB of memory (minimum for boot)
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


; Parse multiboot memory map and find highest available address
; Returns: highest_phys_addr in [highest_phys_addr_low:highest_phys_addr_high]
parse_memory_map:
    pusha

    ; Get multiboot info pointer
    mov esi, [multiboot_info]

    ; Skip multiboot header (8 bytes: total_size + reserved)
    add esi, 8

    ; Initialize highest address to 0
    mov dword [highest_phys_addr_low], 0
    mov dword [highest_phys_addr_high], 0

.find_mmap_tag:
    ; Check if we've reached end tag (type = 0)
    cmp dword [esi], 0
    je .parse_done

    ; Check if this is memory map tag (type = 6)
    cmp dword [esi], 6
    je .process_mmap

    ; Move to next tag (aligned to 8 bytes)
    mov eax, [esi + 4]      ; Get tag size
    add eax, 7              ; Round up to 8-byte boundary
    and eax, ~7
    add esi, eax
    jmp .find_mmap_tag

.process_mmap:
    ; ESI points to memory map tag
    mov ecx, [esi + 4]      ; Get tag size
    sub ecx, 16             ; Subtract tag header size
    mov edx, [esi + 12]     ; Get entry size
    add esi, 16             ; Skip to first entry

.process_entry:
    cmp ecx, 0
    jle .parse_done

    ; Check if this is available memory (type = 1)
    cmp dword [esi + 16], 1
    jne .next_entry

    ; Calculate end address (base + length)
    mov eax, [esi]          ; base_addr_low
    mov ebx, [esi + 4]      ; base_addr_high
    add eax, [esi + 8]      ; + length_low
    adc ebx, [esi + 12]     ; + length_high (with carry)

    ; Compare with current highest address
    cmp ebx, [highest_phys_addr_high]
    ja .update_highest
    jb .next_entry

    ; High parts equal, compare low parts
    cmp eax, [highest_phys_addr_low]
    jbe .next_entry

.update_highest:
    mov [highest_phys_addr_low], eax
    mov [highest_phys_addr_high], ebx

.next_entry:
    add esi, edx            ; Move to next entry
    sub ecx, edx            ; Decrease remaining size
    jmp .process_entry

.parse_done:
    popa
    ret

; Setup dynamic paging based on detected physical memory
setup_dynamic_paging:
    pusha

    ; Calculate how much memory we need to map (round up to 1GB boundary)
    mov eax, [highest_phys_addr_low]
    mov ebx, [highest_phys_addr_high]

    ; For simplicity, cap at 64GB to avoid too many page tables
    cmp ebx, 0x10           ; 64GB = 0x1000000000
    jb .size_ok
    mov ebx, 0x10
    mov eax, 0

.size_ok:
    ; Round up to 1GB boundary (0x40000000)
    add eax, 0x3FFFFFFF
    adc ebx, 0
    and eax, 0xC0000000     ; Clear lower 30 bits

    ; Store the total size to map
    mov [memory_to_map_low], eax
    mov [memory_to_map_high], ebx

    ; Calculate number of 1GB regions needed
    ; Each PDP entry covers 1GB, so we need (total_size / 1GB) entries
    push edx
    mov edx, ebx            ; High part
    mov eax, eax            ; Low part already in eax
    mov ecx, 0x40000000     ; 1GB
    div ecx                 ; EAX = number of 1GB regions
    pop edx

    ; Cap at 512 entries (512GB max)
    cmp eax, 512
    jbe .pdp_entries_ok
    mov eax, 512

.pdp_entries_ok:
    mov [num_pdp_entries], eax

    ; Zero out initial page tables
    mov edi, pml4_table
    mov ecx, 4096 * 6       ; Clear PML4, PDP, and 4 initial PD tables
    xor eax, eax
    rep stosb
    debug_print 'Z'         ; Page Tables Zeroed

    ; Set CR3 to PML4
    mov eax, pml4_table
    mov cr3, eax
    debug_print '4'         ; CR3 Loaded

    ; Setup PML4[0] -> PDP Table
    mov edi, pml4_table
    lea eax, [pdp_table + 3]
    mov [edi], eax

    ; Setup PDP entries and corresponding PD tables
    mov edi, pdp_table
    mov esi, pd_table       ; Start with first PD table
    mov ecx, [num_pdp_entries]

.setup_pdp_loop:
    push ecx

    ; Link PDP entry to PD table
    lea eax, [esi + 3]      ; PD table address + flags
    mov [edi], eax

    ; Fill the PD table with 2MB pages
    push edi
    push esi
    mov edi, esi            ; EDI = current PD table
    mov ebx, 512            ; 512 entries per PD table

    ; Calculate starting physical address for this PD table
    mov eax, [num_pdp_entries]
    sub eax, ecx            ; Current PDP index
    push edx
    mov edx, 0x40000000     ; 1GB per PDP entry
    mul edx                 ; EAX = starting physical address
    pop edx
    or eax, 0x83            ; Add Present + Writable + Large page flags

.fill_pd_loop:
    mov [edi], eax          ; Store PDE
    add edi, 8              ; Next PDE
    add eax, 0x200000       ; Next 2MB physical address
    dec ebx
    jnz .fill_pd_loop

    pop esi
    pop edi

    ; Move to next PDP entry and PD table
    add edi, 8              ; Next PDP entry
    add esi, 4096           ; Next PD table

    pop ecx
    loop .setup_pdp_loop

    popa
    ret

[bits 64]

[extern KernelMain]

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
    default rel
    mov edi, [multiboot_magic] ; EAX holds magic, so EDI will get the 32-bit magic
    mov rsi, [multiboot_info]  ; EBX holds info pointer, but could be 64-bit, so use RSI

    debug_print '9' ; Calling Kernel
    call KernelMain

    ; If the kernel ever returns, hang the system
    debug_print 'R'
    debug_print '!'
.halt:
    cli
    hlt
    jmp .halt