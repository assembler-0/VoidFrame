bits 64

global GdtFlush

GdtFlush:
    lgdt [rdi]          ; Load new GDT
    
    ; Reload segment registers
    mov ax, 0x10        ; Kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to reload CS
    push 0x08           ; Kernel code selector
    lea rax, [rel .flush]
    push rax
    retfq
    
.flush:
    ret