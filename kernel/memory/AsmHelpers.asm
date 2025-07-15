section .text
global EnablePagingAndJump

EnablePagingAndJump:
    mov cr3, rdi
    mov rsp, rdx
    jmp rsi