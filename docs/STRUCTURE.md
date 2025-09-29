# VoidFrame Kernel Structure

```
VoidFrame/
├── arch/x86_64/             # Architechture specific code
│   ├── asm/                 
│   │   └── pxs.asm          
│   ├── cpu/                 
│   │   ├── x64.h            
│   │   └── x64.c            
│   ├── gdt/                 
│   │   ├── GdtTssFlush.asm  
│   │   ├── Gdt.h            
│   │   └── Gdt.c            
│   ├── idt/                 
│   │   ├── IdtLoad.asm      
│   │   ├── Ide.h            
│   │   └── Ide.c            
│   └── interrupts/          
│       ├── Interrupts.asm   
│       ├── Interrupts.c     
│       └── Interrupts.h     
├── drivers/                  # Drivers code
│   ├── ethernet/       
│   │   ├── Packet.h             
│   │   ├── RTL8139.h             
│   │   └── RTL8139.c          
│   ├── PCI/                 
│   │   ├── PCI.h            
│   │   └── PCI.c            
│   ├── ISA/                 
│   │   ├── ISA.h            
│   │   └── ISA.c    
│   ├── LPT/                 
│   │   ├── LPT.h            
│   │   └── LPT.c    
│   ├── sound/                 
│   │   ├── SB16.h            
│   │   └── SB16.c   
│   ├── virtio/       
│   │   ├── Virtio.h             
│   │   ├── VirtioBlk.h             
│   │   └── VirtioBlk.c     
│   ├── RTC/                 
│   │   ├── Rtc.h            
│   │   └── Rtc.c            
│   ├── xHCI/                 
│   │   ├── xHCI.h            
│   │   └── xHCI.c  
│   ├── Ide.h           
│   ├── Ide.c           
│   ├── APIC.h            
│   ├── APIC.c            
│   ├── InterruptController.h            
│   ├── InterruptController.h            
│   ├── Pic.h            
│   ├── Pic.c            
│   ├── PS2.h             
│   ├── PS2.c                         
│   ├── Serial.c       
│   ├── Serial.c       
│   ├── Vesa.c       
│   └── Vesa.h    
├── fs/       
│   ├── FAT1x.h                # Filesystems  
│   ├── FAT12.c           
│   ├── VFRFS.h            
│   ├── VFRFS.c            
│   ├── FsUtils.h                           
│   ├── VFS.c       
│   └── VFS.h    
├── include/                   # Common includes
│   ├── Font.h           
│   ├── Io.h            
│   ├── Switch.h             
│   ├── Switch.asm            
│   ├── stdbool.h            
│   ├── stdint.h            
│   ├── stddef.h            
│   ├── stdlib.h            
│   ├── Ltypes.h            
│   ├── ctypes.h            
│   ├── ctypes.c            
│   └── stdarg.h     
├── kernel/                    # Kernel core
│   ├── atomic/                # Atomic operations
│   │   ├── Atomics.c               
│   │   ├── Atomics.h               
│   │   └── Spilock.h          
│   ├── core/                  # Entry point
│   │   ├── Kernel.h            
│   │   ├── Kernel.c            
│   │   ├── Panic.c            
│   │   ├── Panic.c            
│   │   └── Multiboot2.h            
│   ├── elf/                   # ELF loader
│   │   ├── ELFloader.c  
│   │   └── ElFloader.h            
│   ├── etc/                   # Misc. files
│   │   ├── Console.c      
│   │   ├── Console.h      
│   │   ├── Editor.h            
│   │   ├── Editor.c            
│   │   ├── Shell.c            
│   │   ├── Shell.h            
│   │   ├── StringOps.c            
│   │   ├── StringOps.h            
│   │   ├── VBEConsole.c            
│   │   └── VBEConsole.h     
│   ├── ipc/                  # IPC related files  
│   │   ├── Ipc.c                
│   │   └── Ipc.h         
│   └── sched/                # Scheduler and Process management
│       ├── Shared.h     
│       ├── MLFQ.c     
│       └── MLFQ.h    
├── mm/                       # Physical and Virtual memory manager  
│   ├── KernelHeap.c      
│   ├── KernelHeap.h      
│   ├── MemOps.h      
│   ├── MemOps.c      
│   ├── PMem.h            
│   ├── PMem.c            
│   ├── MemoryPool.c            
│   ├── MemoryPool.h            
│   ├── StackGuard.c            
│   ├── StackGuard.h                        
│   ├── VMem.c            
│   └── VMem.h            
├── scripts/       
│   └── elf.ld               
├── linker.ld         
├── grub.cfg                   
├── meson.build        
├── vfconfig.py        
└── ...                      
```
