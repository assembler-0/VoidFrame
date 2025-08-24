# VoidFrame Kernel Structure

```
VoidFrame/
├── arch/x86_64/             # Architechture specific code
│   ├── asm/                 
│   │   └── pxs.asm          
│   ├── cpu/                 
│   │   ├── Cpu.h            
│   │   └── Cpu.c            
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
│   ├── RTC/                 
│   │   ├── Rtc.h            
│   │   └── Rtc.c            
│   ├── xHCI/                 
│   │   ├── xHCI.h            
│   │   └── xHCI.c  
│   ├── Ide.h           
│   ├── Ide.c           
│   ├── Pic.h            
│   ├── Pic.c            
│   ├── PS2.h             
│   ├── PS2.c                         
│   ├── Serial.c       
│   ├── Serial.c       
│   ├── VesaBIOSExtension.c       
│   └── VesaBIOSExtension.h    
├── fs/       
│   ├── FAT12.h                # Filesystems  
│   ├── FAT12.c           
│   ├── Fs.h            
│   ├── Fs.c            
│   ├── FsUtils.h             
│   ├── FsUtils.c                         
│   ├── VFS.c       
│   └── VFS.h    
├── include/                   # Common includes
│   ├── Font.h           
│   ├── Io.h            
│   ├── Paging.h             
│   ├── Paging.asm            
│   ├── stdbool.h            
│   ├── stdint.h            
│   ├── stddef.h            
│   ├── stdlib.h            
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
│   ├── memory/               # Physical and Virtual memory manager  
│   │   ├── KernelHeap.c      
│   │   ├── KernelHeap.h      
│   │   ├── MemOps.h      
│   │   ├── MemOps.c      
│   │   ├── Memory.h            
│   │   ├── Memory.c            
│   │   ├── MemoryPool.c            
│   │   ├── MemoryPool.h            
│   │   ├── StackGuard.c            
│   │   ├── StackGuard.h                        
│   │   ├── VMem.c            
│   │   └── VMem.h    
│   └── process/              # MLFQ scheduler  
│       ├── Process.c     
│       └── Process.h            
├── scripts/       
│   └── elf.ld               
├── linker.ld         
├── grub.cfg                   
├── meson.build        
└── ...                      
```