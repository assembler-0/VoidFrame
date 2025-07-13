bits 64

global IdtLoad

IdtLoad:
    lidt [rdi]
    ret
