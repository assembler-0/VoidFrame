section .bss
align 4096
pml4_table: resb 4096
pdp_table:  resb 4096
pd_table:   resb 4096
pd_table2:  resb 4096
pd_table3:  resb 4096
pd_table4:  resb 4096
; Reserve space for up to 64 additional PD tables (64GB support)
pd_tables_extended: resb (4096 * 60)

align 16
stack_bottom: resb 16384  ; 16KB stack
stack_top:

section .data
multiboot_magic: dd 0
multiboot_info:  dq 0 ; The info pointer can be a 64-bit address, use dq

; Dynamic memory mapping variables
highest_phys_addr_low:  dd 0
highest_phys_addr_high: dd 0
memory_to_map_low:      dd 0
memory_to_map_high:     dd 0
num_pdp_entries:        dd 0