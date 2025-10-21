; memcmp.asm - Highly optimized memory compare with AVX-512, AVX2, and SSE2
; Based on the C implementation in MemOps.c

global memcmp_internal_sse2
global memcmp_internal_avx2
global memcmp_internal_avx512

section .text

; SSE2 optimized memcmp
memcmp_internal_sse2:
    ; rdi - ptr1
    ; rsi - ptr2
    ; rdx - count
    xor eax, eax
    mov rcx, rdx
    shr rcx, 4 ; count / 16
    jz .sse2_cmp_byte

.sse2_loop:
    movdqu xmm0, [rdi]
    movdqu xmm1, [rsi]
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 0xFFFF
    jne .sse2_found_diff
    add rdi, 16
    add rsi, 16
    dec rcx
    jnz .sse2_loop

.sse2_cmp_byte:
    mov rcx, rdx
    and rcx, 15
    repe cmpsb
    je .equal

.found_diff:
    movzx eax, byte [rdi-1]
    movzx ebx, byte [rsi-1]
    sub eax, ebx
    ret

.sse2_found_diff:
    not eax
    bsf eax, eax
    movzx edx, byte [rdi + rax]
    movzx ecx, byte [rsi + rax]
    sub edx, ecx
    mov eax, edx
    ret

.equal:
    xor eax, eax
    ret

; AVX2 optimized memcmp
memcmp_internal_avx2:
    ; rdi - ptr1
    ; rsi - ptr2
    ; rdx - count
    xor eax, eax
    mov rcx, rdx
    shr rcx, 5 ; count / 32
    jz .avx2_cmp_byte

.avx2_loop:
    vmovdqu ymm0, [rdi]
    vmovdqu ymm1, [rsi]
    vpcmpeqb ymm0, ymm1, ymm0
    vpmovmskb eax, ymm0
    cmp eax, 0xFFFFFFFF
    jne .avx2_found_diff
    add rdi, 32
    add rsi, 32
    dec rcx
    jnz .avx2_loop

.avx2_cmp_byte:
    mov rcx, rdx
    and rcx, 31
    repe cmpsb
    je .avx2_equal
    jmp .avx2_found_diff

.avx2_found_diff:
    not eax
    bsf eax, eax
    movzx edx, byte [rdi + rax]
    movzx ecx, byte [rsi + rax]
    sub edx, ecx
    mov eax, edx
    vzeroupper
    ret

.avx2_equal:
    xor eax, eax
    ret

; AVX-512 optimized memcmp
memcmp_internal_avx512:
    ; rdi - ptr1
    ; rsi - ptr2
    ; rdx - count
    xor eax, eax
    mov rcx, rdx
    shr rcx, 6 ; count / 64
    jz .avx512_cmp_byte

.avx512_loop:
    vmovdqu64 zmm0, [rdi]
    vmovdqu64 zmm1, [rsi]
    vpcmpub k1, zmm0, zmm1, 4
    kmovq rax, k1
    test rax, rax
    jnz .avx512_found_diff
    add rdi, 64
    add rsi, 64
    dec rcx
    jnz .avx512_loop

.avx512_cmp_byte:
    mov rcx, rdx
    and rcx, 63
    repe cmpsb
    je .avx512_equal
    jmp .avx512_found_diff

.avx512_found_diff:
    bsf rax, rax
    movzx edx, byte [rdi + rax]
    movzx ecx, byte [rsi + rax]
    sub edx, ecx
    mov eax, edx
    vzeroupper
    ret

.avx512_equal:
    xor eax, eax
    ret
