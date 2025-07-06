bits 64

global switch_context
global save_context

; void switch_context(ProcessContext* old, ProcessContext* new)
switch_context:
    ; Save old context
    mov [rdi + 0], rax
    mov [rdi + 8], rbx
    mov [rdi + 16], rcx
    mov [rdi + 24], rdx
    mov [rdi + 32], rsi
    mov [rdi + 40], rdi
    mov [rdi + 48], rbp
    mov [rdi + 56], rsp
    mov [rdi + 64], r8
    mov [rdi + 72], r9
    mov [rdi + 80], r10
    mov [rdi + 88], r11
    mov [rdi + 96], r12
    mov [rdi + 104], r13
    mov [rdi + 112], r14
    mov [rdi + 120], r15
    
    ; Save RIP (return address)
    mov rax, [rsp]
    mov [rdi + 128], rax
    
    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + 136], rax
    
    ; Load new context
    mov rax, [rsi + 0]
    mov rbx, [rsi + 8]
    mov rcx, [rsi + 16]
    mov rdx, [rsi + 24]
    mov rbp, [rsi + 48]
    mov rsp, [rsi + 56]
    mov r8, [rsi + 64]
    mov r9, [rsi + 72]
    mov r10, [rsi + 80]
    mov r11, [rsi + 88]
    mov r12, [rsi + 96]
    mov r13, [rsi + 104]
    mov r14, [rsi + 112]
    mov r15, [rsi + 120]
    
    ; Load RFLAGS
    mov rdi, [rsi + 136]
    push rdi
    popfq
    
    ; Load RSI and RDI last
    mov rdi, [rsi + 40]
    mov rsi, [rsi + 32]
    
    ret