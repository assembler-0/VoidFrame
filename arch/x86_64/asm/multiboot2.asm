section .multiboot
header_start:
    dd 0xE85250D6                ; Multiboot2 magic number
    dd 0                         ; Architecture 0 (protected mode i386)
    dd header_end - header_start ; header length
    ; checksum = -(magic + arch + length)
    dd -(0xE85250D6 + 0 + (header_end - header_start))

%ifdef VF_CONFIG_VESA_FB
    ; Framebuffer tag - request specific graphics mode
    align 8
framebuffer_tag_start:
    dw 5        ; type = framebuffer
    dw 0        ; flags
    dd 24       ; size
    dd 800      ; width
    dd 600      ; height
    dd 32       ; depth (bits per pixel)
framebuffer_tag_end:

    ; VBE tag - request VBE info
    align 8
vbe_tag_start:
    dw 7        ; type = VBE
    dw 0        ; flags
    dd vbe_tag_end - vbe_tag_start ; size
vbe_tag_end:
%endif

    ; End tag - required
    align 8
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end: