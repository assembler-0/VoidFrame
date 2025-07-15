bits 64

extern Syscall

global SyscallEntry

SyscallEntry:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; System call convention: rax=syscall_num, rdi=arg1, rsi=arg2, rdx=arg3
    mov rdi, rax    ; syscall number
    mov rsi, rbx    ; arg1 
    mov rdx, rcx    ; arg2
    mov rcx, r8     ; arg3
    
    call Syscall
    
    ; Return value in rax is already set
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax      ; Pop original rax, but leave the syscall return value in rax
    
    iretq