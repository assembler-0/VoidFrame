check_and_enable_features:
    ; Test for CPUID capability (ID bit in EFLAGS)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21 ; Flip the ID bit
    push eax
    popfd
    pushfd
    pop eax
    xor eax, ecx
    jz .no_cpuid_present ; If ID bit couldn't be flipped, CPUID not supported.

    debug_print 'C' ; CPUID detected

    ; Get basic CPU features (CPUID Leaf 1)
    mov eax, 1
    cpuid

    ; EAX = Version Info (Model, Family, Stepping)
    ; EBX = Brand Index, CLFLUSH size, Number of Logical Processors, APIC ID
    ; ECX = Feature flags 1 (SSE3, SSSE3, SSE4.1, SSE4.2, AVX, etc.)
    ; EDX = Feature flags 2 (FPU, VME, MMX, SSE, SSE2, FXSR, MSR, PAE, APIC, PGE, PSN, CMOV, MTRR, HTT, etc.)

    ; --- Enable FPU/SSE/SSE2/XMM support ---
    ; This is required for modern C/C++ compiled code that uses floating point.
    ; CR0.EM (Emulation) should be 0.
    ; CR0.MP (Monitor Coprocessor) should be 1.
    ; CR4.OSFXSR (Operating System FXSAVE/FXRSTOR Support) should be 1.
    ; CR4.OSXMMEXCPT (Operating System Unmasked SIMD Floating-Point Exception Support) should be 1.

    ; Check for SSE support (EDX bit 25)
    test edx, 1 << 25   ; SSE
    jz .skip_sse_enable

    mov eax, cr0
    and eax, ~(1 << 2)  ; Clear EM (Emulation)
    or eax, 1 << 1      ; Set MP (Monitor Coprocessor)
    mov cr0, eax

    mov eax, cr4
    or eax, 1 << 9      ; Set OSFXSR (Operating System FXSAVE/FXRSTOR Support)
    or eax, 1 << 10     ; Set OSXMMEXCPT (Operating System Unmasked SIMD Floating-Point Exception Support)
    mov cr4, eax
    debug_print 'S' ; SSE Enabled

.skip_sse_enable:

    ; --- Enable Write Protect (WP) in CR0 ---
    ; CR0.WP (bit 16) protects read-only pages from ring 0 writes.
    ; This is crucial for proper memory protection.
    mov eax, cr0
    or eax, 1 << 16     ; Set WP bit
    mov cr0, eax
    debug_print 'W' ; Write Protect Enabled

    ; --- Enable No-Execute (NXE) in EFER MSR ---
    ; This feature helps prevent buffer overflow attacks by marking memory pages as non-executable.
    ; Requires PAE to be enabled.
    ; Check for NX support (EDX bit 20 from CPUID leaf 1)
    mov eax, 1
    cpuid
    test edx, 1 << 20   ; NX (Execute Disable Bit)
    jz .skip_nxe_enable

    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, 1 << 11     ; Set NXE (No-Execute Enable) bit
    wrmsr
    debug_print 'N' ; NX Enabled

.skip_nxe_enable:

.no_cpuid_present:
    ret
