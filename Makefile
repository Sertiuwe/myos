CC      = clang
LD      = ld.lld
NASM    = nasm
CFLAGS  = --target=x86_64-unknown-none -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=small -O2 -mgeneral-regs-only -fno-asynchronous-unwind-tables
LDFLAGS = -T linker.ld -nostdlib -static

all: os.img

boot.bin: boot.asm
	$(NASM) -f bin boot.asm -o boot.bin

stage2.bin: stage2.asm
	$(NASM) -f bin stage2.asm -o stage2.bin
	truncate -s 16384 stage2.bin

loader.o: loader.c memory.h
	$(CC) $(CFLAGS) -c loader.c -o loader.o

memory.o: memory.c memory.h
	$(CC) $(CFLAGS) -c memory.c -o memory.o

kernel.elf: loader.o memory.o
	$(LD) $(LDFLAGS) -o kernel.elf loader.o memory.o

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	truncate -s 32768 kernel.bin

os.img: boot.bin stage2.bin kernel.bin
	cat boot.bin stage2.bin kernel.bin > os.img
	truncate -s 1474560 os.img

run: os.img
	qemu-system-x86_64 -drive file=os.img,format=raw -vnc :1 -display none -serial stdio

clean:
	rm -f *.o *.bin *.elf os.img

.PHONY: all clean run
