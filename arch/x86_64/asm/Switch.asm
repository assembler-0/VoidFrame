bits 64

global SwitchContext

; void SwitchContext(ProcessContext* old, ProcessContext* new)
; ProcessContext is now same as Registers struct
SwitchContext:
    ; Save current context to old (matching Registers struct layout)
    mov [rdi + 0],   r15
    mov [rdi + 8],   r14
    mov [rdi + 16],  r13
    mov [rdi + 24],  r12
    mov [rdi + 32],  r11
    mov [rdi + 40],  r10
    mov [rdi + 48],  r9
    mov [rdi + 56],  r8
    mov [rdi + 64],  rbp
    mov [rdi + 72],  rsi
    mov [rdi + 80],  rdi
    mov [rdi + 88],  rdx
    mov [rdi + 96],  rcx
    mov [rdi + 104], rbx
    mov [rdi + 112], rax

    ; Save segment registers
    mov ax, ds
    mov [rdi + 120], rax
    mov ax, es
    mov [rdi + 128], rax
    mov ax, fs
    mov [rdi + 136], rax
    mov ax, gs
    mov [rdi + 144], rax

    ; Skip interrupt_number and error_code (152, 160)

    ; Save return address as RIP
    mov rax, [rsp]
    mov [rdi + 168], rax

    ; Save CS (assume kernel code segment)
    mov word [rdi + 176], 0x08

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + 184], rax

    ; Save RSP (after return address)
    mov rax, rsp
    add rax, 8  ; Account for return address
    mov [rdi + 192], rax

    ; Save SS (assume kernel data segment)
    mov word [rdi + 200], 0x10

    ; Load new context from new
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov r11, [rsi + 32]
    mov r10, [rsi + 40]
    mov r9,  [rsi + 48]
    mov r8,  [rsi + 56]
    mov rbp, [rsi + 64]
    ; rsi will be loaded last
    mov rdi, [rsi + 80]
    mov rdx, [rsi + 88]
    mov rcx, [rsi + 96]
    mov rbx, [rsi + 104]
    mov rax, [rsi + 112]

    ; Load segment registers
    mov ax, [rsi + 120]
    mov ds, ax
    mov ax, [rsi + 128]
    mov es, ax
    mov ax, [rsi + 136]
    mov fs, ax
    mov ax, [rsi + 144]
    mov gs, ax

    ; Load RSP
    mov rsp, [rsi + 192]

    ; Load RFLAGS
    push qword [rsi + 184]
    popfq

    ; Load new RIP and jump (load rsi last)
    push qword [rsi + 168]
    mov rsi, [rsi + 72]  ; Load rsi last

    ret  ; Jump to new RIP