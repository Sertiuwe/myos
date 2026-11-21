CC      = clang
LD      = ld.lld
NASM    = nasm
CFLAGS  = --target=x86_64-unknown-none -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=small -O2 -mgeneral-regs-only -fno-asynchronous-unwind-tables -Wall -Wextra -I.
LDFLAGS = -T linker.ld -nostdlib -static

KZONE   = 131072
OBJS    = loader.o memory.o memtest.o kstring.o con.o memmap.o kbd.o shell.o

all: os.img

boot.bin: boot.asm
	$(NASM) -f bin boot.asm -o boot.bin

stage2.bin: stage2.asm
	$(NASM) -f bin stage2.asm -o stage2.bin
	truncate -s 16384 stage2.bin

loader.o: loader.c memory.h kstring.h
	$(CC) $(CFLAGS) -c loader.c -o loader.o

memory.o: memory.c memory.h kstring.h
	$(CC) $(CFLAGS) -c memory.c -o memory.o

memtest.o: memtest.c memory.h kstring.h
	$(CC) $(CFLAGS) -c memtest.c -o memtest.o

kstring.o: kstring.c kstring.h
	$(CC) $(CFLAGS) -c kstring.c -o kstring.o

con.o: con.c con.h gfx.h kstring.h
	$(CC) $(CFLAGS) -c con.c -o con.o

memmap.o: memmap.c memmap.h memory.h con.h kstring.h
	$(CC) $(CFLAGS) -c memmap.c -o memmap.o

kbd.o: kbd.c kbd.h
	$(CC) $(CFLAGS) -c kbd.c -o kbd.o

shell.o: shell.c shell.h con.h kbd.h memory.h memmap.h kstring.h
	$(CC) $(CFLAGS) -c shell.c -o shell.o

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	@sz=$$(stat -c %s kernel.bin); \
	if [ $$sz -gt $(KZONE) ]; then \
		echo "ERROR: kernel.bin = $$sz bytes > $(KZONE). Увеличь KZONE и секторы в boot.asm"; \
		exit 1; \
	fi; \
	echo "kernel.bin = $$sz / $(KZONE) bytes"
	truncate -s $(KZONE) kernel.bin

os.img: boot.bin stage2.bin kernel.bin
	cat boot.bin stage2.bin kernel.bin > os.img
	truncate -s 1474560 os.img

run: os.img
	qemu-system-x86_64 -drive file=os.img,format=raw -vnc :1 -display none -serial stdio

clean:
	rm -f *.o *.bin *.elf os.img

.PHONY: all clean run
