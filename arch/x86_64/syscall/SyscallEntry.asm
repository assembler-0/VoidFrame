bits 64
extern Syscall
global SyscallEntry

SyscallEntry:
    ; Save all registers EXCEPT rax, which we handle specially
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

    ; Save the original rax (syscall number) separately
    push rax

    stac

    ; Setup parameters for Syscall(num, arg1, arg2, arg3)
    ; We just need to get the syscall number (original rax) into rdi
    mov rdi, [rsp]  ; 1st param: syscall number (from the saved rax)
    mov rsi, r11    ; 2nd param: arg1 (already in rsi)
    mov rdx, r12    ; 3rd param: arg2 (already in rdx)
    mov rcx, r13    ; 4th param: arg3 (already in rcx)

    call Syscall
    ; C function return value is now in rax

    clac

    ; Discard the saved rax from the stack. The return value is safe in rax.
    add rsp, 8

    ; Restore all other registers
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

    iretq