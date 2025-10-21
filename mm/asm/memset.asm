; memset.asm - Highly optimized memory set with AVX-512, AVX2, and SSE2
; Based on the C implementation in MemOps.c

global memset_internal_sse2
global memset_internal_avx2
global memset_internal_avx512

section .text

; SSE2 optimized memset
memset_internal_sse2:
    ; rdi - dest
    ; rsi - value
    ; rdx - count
    push rbx
    push rcx
    
    movzx ecx, sil
    mov eax, ecx
    imul eax, 0x01010101
    
    movd xmm0, eax
    punpckldq xmm0, xmm0
    punpcklqdq xmm0, xmm0

    mov rcx, rdx
    shr rcx, 6 ; count / 64
    jz .sse2_copy_16_bytes

.sse2_loop_64:
    movdqu [rdi], xmm0
    movdqu [rdi + 16], xmm0
    movdqu [rdi + 32], xmm0
    movdqu [rdi + 48], xmm0
    add rdi, 64
    dec rcx
    jnz .sse2_loop_64

.sse2_copy_16_bytes:
    mov rcx, rdx
    and rcx, 63
    shr rcx, 4 ; count / 16
    jz .sse2_done

.sse2_loop_16:
    movdqu [rdi], xmm0
    add rdi, 16
    dec rcx
    jnz .sse2_loop_16

.sse2_done:
    pop rcx
    pop rbx
    ret

; AVX2 optimized memset
memset_internal_avx2:
    ; rdi - dest
    ; rsi - value
    ; rdx - count
    push rbx
    push rcx

    movzx ecx, sil
    mov eax, ecx
    imul eax, 0x01010101
    
    vmovd xmm0, eax
    vpbroadcastd ymm0, xmm0

    mov rcx, rdx
    shr rcx, 8 ; count / 256
    jz .avx2_copy_32_bytes

.avx2_loop_256:
    vmovdqu [rdi], ymm0
    vmovdqu [rdi + 32], ymm0
    vmovdqu [rdi + 64], ymm0
    vmovdqu [rdi + 96], ymm0
    vmovdqu [rdi + 128], ymm0
    vmovdqu [rdi + 160], ymm0
    vmovdqu [rdi + 192], ymm0
    vmovdqu [rdi + 224], ymm0
    add rdi, 256
    dec rcx
    jnz .avx2_loop_256

.avx2_copy_32_bytes:
    mov rcx, rdx
    and rcx, 255
    shr rcx, 5 ; count / 32
    jz .avx2_done

.avx2_loop_32:
    vmovdqu [rdi], ymm0
    add rdi, 32
    dec rcx
    jnz .avx2_loop_32

.avx2_done:
    vzeroupper
    pop rcx
    pop rbx
    ret

; AVX-512 optimized memset
memset_internal_avx512:
    ; rdi - dest
    ; rsi - value
    ; rdx - count
    push rbx
    push rcx

    movzx ecx, sil
    mov eax, ecx
    imul eax, 0x01010101

    vmovd xmm0, eax
    vpbroadcastd zmm0, xmm0

    mov rcx, rdx
    shr rcx, 9 ; count / 512
    jz .avx512_copy_64_bytes

.avx512_loop_512:
    vmovdqu64 [rdi], zmm0
    vmovdqu64 [rdi + 64], zmm0
    vmovdqu64 [rdi + 128], zmm0
    vmovdqu64 [rdi + 192], zmm0
    vmovdqu64 [rdi + 256], zmm0
    vmovdqu64 [rdi + 320], zmm0
    vmovdqu64 [rdi + 384], zmm0
    vmovdqu64 [rdi + 448], zmm0
    add rdi, 512
    dec rcx
    jnz .avx512_loop_512

.avx512_copy_64_bytes:
    mov rcx, rdx
    and rcx, 511
    shr rcx, 6 ; count / 64
    jz .avx512_done

.avx512_loop_64:
    vmovdqu64 [rdi], zmm0
    add rdi, 64
    dec rcx
    jnz .avx512_loop_64

.avx512_done:
    vzeroupper
    pop rcx
    pop rbx
    ret
