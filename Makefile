CC      = clang
LD      = ld.lld
NASM    = nasm
CFLAGS  = --target=x86_64-unknown-none -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=small -O2 -mgeneral-regs-only -fno-asynchronous-unwind-tables -Wall -Wextra -I.
LDFLAGS = -T linker.ld -nostdlib -static

KZONE   = 131072
OBJS    = loader.o memory.o memtest.o kstring.o con.o memmap.o kbd.o shell.o kstdio.o kctype.o kstdlib.o kpanic.o bdev.o ata.o rtc.o bcache.o myfs.o idt.o isr_stubs.o pic.o pit.o irq.o pci.o mouse.o cursor.o

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

kstdio.o: kstdio.c kstdio.h kstring.h
	$(CC) $(CFLAGS) -c kstdio.c -o kstdio.o

kctype.o: kctype.c kctype.h
	$(CC) $(CFLAGS) -c kctype.c -o kctype.o

kstdlib.o: kstdlib.c kstdlib.h kctype.h kerrno.h
	$(CC) $(CFLAGS) -c kstdlib.c -o kstdlib.o

kpanic.o: kpanic.c kpanic.h kstdio.h
	$(CC) $(CFLAGS) -c kpanic.c -o kpanic.o

bdev.o: bdev.c bdev.h kstring.h
	$(CC) $(CFLAGS) -c bdev.c -o bdev.o

ata.o: ata.c ata.h bdev.h kstdio.h
	$(CC) $(CFLAGS) -c ata.c -o ata.o

rtc.o: rtc.c rtc.h
	$(CC) $(CFLAGS) -c rtc.c -o rtc.o

bcache.o: bcache.c bcache.h bdev.h memory.h kstring.h
	$(CC) $(CFLAGS) -c bcache.c -o bcache.o

myfs.o: myfs.c myfs.h bdev.h bcache.h memory.h kstring.h rtc.h kstdio.h
	$(CC) $(CFLAGS) -c myfs.c -o myfs.o

idt.o: idt.c idt.h kstdio.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o

isr_stubs.o: isr_stubs.asm
	$(NASM) -f elf64 isr_stubs.asm -o isr_stubs.o

pic.o: pic.c pic.h
	$(CC) $(CFLAGS) -c pic.c -o pic.o

pit.o: pit.c pit.h pic.h kstdio.h
	$(CC) $(CFLAGS) -c pit.c -o pit.o

irq.o: irq.c pic.h pit.h kstdio.h
	$(CC) $(CFLAGS) -c irq.c -o irq.o

pci.o: pci.c pci.h kstdio.h kstring.h
	$(CC) $(CFLAGS) -c pci.c -o pci.o

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

fs.img:
	@if [ ! -f fs.img ]; then truncate -s 8388608 fs.img; echo "created fs.img (8 MiB)"; fi

run: os.img fs.img
	qemu-system-x86_64 -drive file=os.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 -vnc :1 -display none -serial stdio

clean:
	rm -f *.o *.bin *.elf os.img

.PHONY: all clean run

	$(CC) $(CFLAGS) -c mouse.c -o mouse.o

	$(CC) $(CFLAGS) -c cursor.c -o cursor.o

	$(CC) $(CFLAGS) -c mouse.c -o mouse.o

	$(CC) $(CFLAGS) -c cursor.c -o cursor.o

mouse.o: mouse.c mouse.h pic.h kstdio.h
	$(CC) $(CFLAGS) -c mouse.c -o mouse.o

cursor.o: cursor.c cursor.h kstring.h
	$(CC) $(CFLAGS) -c cursor.c -o cursor.o
