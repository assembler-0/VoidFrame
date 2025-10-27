; Macro for debugging output on Bochs/QEMU's 0xE9 port
%macro debug_print 1
    mov dx, 0xE9
    mov al, %1
    out dx, al
%endmacro