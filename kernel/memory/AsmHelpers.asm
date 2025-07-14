section .text
global EnablePagingAndJump

EnablePagingAndJump:
    mov cr3, rdi
    jmp rsi