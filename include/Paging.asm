section .text
global JumpToKernelHigherHalf

JumpToKernelHigherHalf:
    mov rsp, rsi
    jmp rdi