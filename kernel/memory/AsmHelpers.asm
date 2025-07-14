section .text
global EnablePagingAndJump

EnablePagingAndJump:
    mov rdi, cr3
    jmp rsi