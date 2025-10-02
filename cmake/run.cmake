# ============================================================================
# Run Targets
# ============================================================================
add_custom_target(run
        COMMAND ${QEMU_SYSTEM_X86_64}
            -cpu max
            -vga vmware
            -enable-kvm
            -cdrom ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso
            -debugcon file:bootstrap.log
            -serial stdio
            -no-reboot -no-shutdown
            -m 4G
            -drive file=VoidFrameDisk.img,if=ide
            -drive file=SataDisk.img,if=none,id=sata0
            -device ahci,id=ahci
            -device ide-hd,drive=sata0,bus=ahci.0
            -boot d
            -device rtl8139
            -device e1000
            -device nec-usb-xhci,id=xhci
            -device ich9-intel-hda
            -usb -device usb-tablet
            -audio pa,id=myaudio
            -device sb16,iobase=0x220,irq=5,dma=1,dma16=5
            -parallel file:printer.out
            -drive file=VirtioDisk.img,format=raw,id=virtio_disk,if=none
            -device virtio-blk-pci,drive=virtio_disk,disable-legacy=on
        DEPENDS iso img extra-img
)

add_custom_target(runmin
        COMMAND ${QEMU_SYSTEM_X86_64}
            -cdrom ${CMAKE_CURRENT_BINARY_DIR}/VoidFrame.iso
            -nographic
            -debugcon file:bootstrap.log
            -serial file:serial.log
            -no-reboot -no-shutdown
            -m 1G
            -boot d
        DEPENDS iso
)

add_custom_target(img
        COMMAND ${QEMU_IMG} create -f qcow2 VoidFrameDisk.img 16G
        COMMAND ${MKFS_EXT2} VoidFrameDisk.img
        COMMENT "Creating disk images"
)

add_custom_target(extra-img
        COMMAND ${QEMU_IMG} create -f raw VirtioDisk.img 128M
        COMMAND ${MKFS_FAT} -F 16 VirtioDisk.img
        COMMAND ${QEMU_IMG} create -f raw SataDisk.img 128M
        COMMAND ${MKFS_FAT} -F 16 SataDisk.img
        COMMENT "Creating extra disk images"
)

add_custom_target(dump
        COMMAND ${LLVM_OBJDUMP} -d $<TARGET_FILE:voidframe.krnl> > voidframe.dump
        COMMAND ${LLVM_OBJDUMP} -t $<TARGET_FILE:voidframe.krnl> > voidframe.sym
        DEPENDS voidframe.krnl
        COMMENT "Generating disassembly and symbols"
)
