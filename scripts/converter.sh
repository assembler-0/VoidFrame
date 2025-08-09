#!/bin/bash
# Use -depth 8 RGB: to force RGB format (3 bytes per pixel) without alpha
convert splash1.png -depth 8 -resize 800x600! RGB:splash1.raw
convert splash2.png -depth 8 -resize 800x600! RGB:splash2.raw
convert splash3.png -depth 8 -resize 800x600! RGB:splash3.raw
convert splash4.png -depth 8 -resize 800x600! RGB:splash4.raw

# Then pack to 32-bit format for your framebuffer
for i in 1 2 3 4; do
    python3 -c "
import struct
with open('splash${i}.raw', 'rb') as f:
    data = f.read()
with open('splash${i}_32.raw', 'wb') as f:
    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i+1], data[i+2]
        # Pack as 0x00RRGGBB (32-bit)
        f.write(struct.pack('<I', (r << 16) | (g << 8) | b))
"
done

# Fixed objcopy commands (you had some output filename typos)
objcopy --input binary --output elf64-x86-64 --binary-architecture i386 splash1_32.raw splash1.o
objcopy --input binary --output elf64-x86-64 --binary-architecture i386 splash2_32.raw splash2.o
objcopy --input binary --output elf64-x86-64 --binary-architecture i386 splash3_32.raw splash3.o
objcopy --input binary --output elf64-x86-64 --binary-architecture i386 splash4_32.raw splash4.o
