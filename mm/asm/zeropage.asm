; zeropage.asm - Highly optimized page zeroing with AVX-512, AVX2, and SSE2
; Based on the C implementation in MemOps.c

global zeropage_internal_sse2
global zeropage_internal_avx2
global zeropage_internal_avx512

section .text

; SSE2 optimized zeropage
zeropage_internal_sse2:
    ; rdi - page address
    push rbx
    push rcx

    pxor xmm0, xmm0

    mov rcx, 4096
    shr rcx, 7 ; 4096 / 128

.sse2_loop:
    movdqa [rdi], xmm0
    movdqa [rdi + 16], xmm0
    movdqa [rdi + 32], xmm0
    movdqa [rdi + 48], xmm0
    movdqa [rdi + 64], xmm0
    movdqa [rdi + 80], xmm0
    movdqa [rdi + 96], xmm0
    movdqa [rdi + 112], xmm0
    add rdi, 128
    dec rcx
    jnz .sse2_loop

    mfence
    pop rcx
    pop rbx
    ret

; AVX2 optimized zeropage
zeropage_internal_avx2:
    ; rdi - page address
    push rbx
    push rcx

    vpxor ymm0, ymm0, ymm0

    mov rcx, 4096
    shr rcx, 8 ; 4096 / 256

.avx2_loop:
    vmovdqa [rdi], ymm0
    vmovdqa [rdi + 32], ymm0
    vmovdqa [rdi + 64], ymm0
    vmovdqa [rdi + 96], ymm0
    vmovdqa [rdi + 128], ymm0
    vmovdqa [rdi + 160], ymm0
    vmovdqa [rdi + 192], ymm0
    vmovdqa [rdi + 224], ymm0
    add rdi, 256
    dec rcx
    jnz .avx2_loop

    vzeroupper
    mfence
    pop rcx
    pop rbx
    ret

; AVX-512 optimized zeropage
zeropage_internal_avx512:
    ; rdi - page address
    push rbx
    push rcx

    vpxorq zmm0, zmm0, zmm0

    mov rcx, 4096
    shr rcx, 9 ; 4096 / 512

.avx512_loop:
    vmovdqa64 [rdi], zmm0
    vmovdqa64 [rdi + 64], zmm0
    vmovdqa64 [rdi + 128], zmm0
    vmovdqa64 [rdi + 192], zmm0
    vmovdqa64 [rdi + 256], zmm0
    vmovdqa64 [rdi + 320], zmm0
    vmovdqa64 [rdi + 384], zmm0
    vmovdqa64 [rdi + 448], zmm0
    add rdi, 512
    dec rcx
    jnz .avx512_loop

    vzeroupper
    mfence
    pop rcx
    pop rbx
    ret
