section .text
global SwitchToHigherHalf

SwitchToHigherHalf:
    mov cr3, rdi
    mov rsp, rdx
    jmp rsi