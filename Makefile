ASM = nasm
CC = ccache clang
LD = ld

ASMFLAGS = -f elf64
CFLAGS = -m64 -Wall -O -fno-omit-frame-pointer -finline-functions -nostdinc -fno-builtin -I./Kernel -ffreestanding -fno-stack-protector
LDFLAGS = -T linker.ld -melf_x86_64 --no-warn-rwx-segments

all: clean voidframe.krnl run

boot.o: Boot/boot.asm
	$(ASM) $(ASMFLAGS) -o $@ $<

kernel.o: Kernel/Kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

idt.o: Kernel/Idt.c
	$(CC) $(CFLAGS) -c -o $@ $<

idt_load.o: Kernel/IdtLoad.asm
	$(ASM) $(ASMFLAGS) -o $@ $<

interrupts.o: Kernel/Interrupts.c
	$(CC) $(CFLAGS) -c -o $@ $<

interrupts_s.o: Kernel/Interrupts.asm
	$(ASM) $(ASMFLAGS) -o $@ $<

pic.o: Kernel/Pic.c
	$(CC) $(CFLAGS) -c -o $@ $<

memory.o: Kernel/Memory.c
	$(CC) $(CFLAGS) -c -o $@ $<

cpu.o: Kernel/Cpu.c
	$(CC) $(CFLAGS) -c -o $@ $<

memops.o: Kernel/MemOps.c
	$(CC) $(CFLAGS) -c -o $@ $<

vmem.o: Kernel/VMem.c
	$(CC) $(CFLAGS) -c -o $@ $<

process.o: Kernel/Process.c
	$(CC) $(CFLAGS) -c -o $@ $<

switch.o: Kernel/Switch.asm
	$(ASM) $(ASMFLAGS) -o $@ $<

voidframe.krnl: boot.o kernel.o idt.o idt_load.o interrupts.o interrupts_s.o pic.o memory.o cpu.o memops.o vmem.o process.o switch.o
	$(LD) $(LDFLAGS) -o $@ $^

run: iso
	qemu-system-x86_64 -cdrom VoidFrame.iso -m 1G -d int,cpu_reset -no-reboot -no-shutdown

iso: voidframe.krnl
	mkdir -p isodir/boot/grub
		cp voidframe.krnl isodir/boot/voidframe.krnl
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o VoidFrame.iso isodir

clean:
	rm -f *.o *.bin *.iso *.krnl
	rm -rf isodir
