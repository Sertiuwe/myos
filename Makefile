CC      = clang
LD      = ld.lld
NASM    = nasm
CFLAGS  = --target=x86_64-unknown-none -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=small -O2 -mgeneral-regs-only -fno-asynchronous-unwind-tables -Wall -Wextra -I. -Iinc -Iapp
LDFLAGS = -T linker.ld -nostdlib -static

KZONE   = 196608
OBJS    = loader.o memory.o memtest.o kstring.o con.o memmap.o kbd.o shell.o kstdio.o kctype.o kstdlib.o kpanic.o bdev.o ata.o rtc.o bcache.o myfs.o idt.o isr_stubs.o pic.o pit.o irq.o pci.o mouse.o cursor.o asset.o graphic_api.o graphic_server.o app_demo.o myfsapp.o sched.o sched_switch.o fslock.o term.o syscall.o syslib.o config.o syscall_entry.o kernel.o elfload.o app_blob.o studio_blob.o gui_blob.o dma.o osdisk.o ga_remote.o fblock.o sxnl.o

all: os.img

boot.bin: boot/boot.asm
	$(NASM) -f bin boot/boot.asm -o boot.bin

stage2.bin: boot/stage2.asm
	$(NASM) -f bin boot/stage2.asm -o stage2.bin
	truncate -s 16384 stage2.bin

loader.o: core/loader.c inc/memory.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/loader.c -o loader.o

memory.o: mm/memory.c inc/memory.h inc/kstring.h
	$(CC) $(CFLAGS) -c mm/memory.c -o memory.o

memtest.o: mm/memtest.c inc/memory.h inc/kstring.h
	$(CC) $(CFLAGS) -c mm/memtest.c -o memtest.o

kstring.o: core/kstring.c inc/kstring.h
	$(CC) $(CFLAGS) -c core/kstring.c -o kstring.o

con.o: core/con.c inc/con.h inc/gfx.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/con.c -o con.o

memmap.o: mm/memmap.c inc/memmap.h inc/memory.h inc/con.h inc/kstring.h
	$(CC) $(CFLAGS) -c mm/memmap.c -o memmap.o

kbd.o: core/kbd.c inc/kbd.h
	$(CC) $(CFLAGS) -c core/kbd.c -o kbd.o

shell.o: core/shell.c inc/shell.h inc/con.h inc/kbd.h inc/memory.h inc/memmap.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/shell.c -o shell.o

kstdio.o: core/kstdio.c inc/kstdio.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/kstdio.c -o kstdio.o

kctype.o: core/kctype.c inc/kctype.h
	$(CC) $(CFLAGS) -c core/kctype.c -o kctype.o

kstdlib.o: core/kstdlib.c inc/kstdlib.h inc/kctype.h inc/kerrno.h
	$(CC) $(CFLAGS) -c core/kstdlib.c -o kstdlib.o

kpanic.o: core/kpanic.c inc/kpanic.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/kpanic.c -o kpanic.o

bdev.o: fs/bdev.c inc/bdev.h inc/kstring.h
	$(CC) $(CFLAGS) -c fs/bdev.c -o bdev.o

ata.o: fs/ata.c inc/ata.h inc/bdev.h inc/kstdio.h
	$(CC) $(CFLAGS) -c fs/ata.c -o ata.o

rtc.o: core/rtc.c inc/rtc.h
	$(CC) $(CFLAGS) -c core/rtc.c -o rtc.o

bcache.o: mm/bcache.c inc/bcache.h inc/bdev.h inc/memory.h inc/kstring.h
	$(CC) $(CFLAGS) -c mm/bcache.c -o bcache.o

myfs.o: fs/myfs.c inc/myfs.h inc/bdev.h inc/bcache.h inc/memory.h inc/kstring.h inc/rtc.h inc/kstdio.h
	$(CC) $(CFLAGS) -c fs/myfs.c -o myfs.o

idt.o: core/idt.c inc/idt.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/idt.c -o idt.o

isr_stubs.o: core/isr_stubs.asm
	$(NASM) -f elf64 core/isr_stubs.asm -o isr_stubs.o

pic.o: core/pic.c inc/pic.h
	$(CC) $(CFLAGS) -c core/pic.c -o pic.o

pit.o: core/pit.c inc/pit.h inc/pic.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/pit.c -o pit.o

irq.o: core/irq.c inc/pic.h inc/pit.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/irq.c -o irq.o

pci.o: core/pci.c inc/pci.h inc/kstdio.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/pci.c -o pci.o

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJS)

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	@sz=$$(stat -c %s kernel.bin); \
	if [ $$sz -gt $(KZONE) ]; then \
		echo "ERROR: kernel.bin = $$sz bytes > $(KZONE). Увеличь KZONE и секторы в boot/boot.asm"; \
		exit 1; \
	fi; \
	echo "kernel.bin = $$sz / $(KZONE) bytes"
	truncate -s $(KZONE) kernel.bin

osboot.img: boot.bin stage2.bin kernel.bin
	cat boot.bin stage2.bin kernel.bin > osboot.img
	truncate -s 1048576 osboot.img

osvol.img:
	@if [ ! -f osvol.img ]; then \
		truncate -s 7340032 osvol.img; \
		python3 mkmyfs.py osvol.img --base 0 --blocks 14336; \
	fi

os.img: osboot.img osvol.img
	cat osboot.img osvol.img > os.img

fs.img:
	@if [ ! -f fs.img ]; then truncate -s 8388608 fs.img; echo "created fs.img (8 MiB)"; fi

run: os.img fs.img
	qemu-system-x86_64 -drive file=os.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 -vnc :1 -display none -serial stdio

clean:
	rm -f *.o *.bin *.elf os.img osboot.img

.PHONY: all clean run

	$(CC) $(CFLAGS) -c core/mouse.c -o mouse.o

	$(CC) $(CFLAGS) -c gfx/cursor.c -o cursor.o

	$(CC) $(CFLAGS) -c core/mouse.c -o mouse.o

	$(CC) $(CFLAGS) -c gfx/cursor.c -o cursor.o

mouse.o: core/mouse.c inc/mouse.h inc/pic.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/mouse.c -o mouse.o

cursor.o: gfx/cursor.c inc/cursor.h inc/kstring.h
	$(CC) $(CFLAGS) -c gfx/cursor.c -o cursor.o

asset.o: gfx/asset.c inc/asset.h inc/kstring.h
	$(CC) $(CFLAGS) -c gfx/asset.c -o asset.o

graphic_api.o: gfx/graphic_api.c inc/graphic_api.h inc/gfx.h inc/asset.h inc/kstring.h
	$(CC) $(CFLAGS) -c gfx/graphic_api.c -o graphic_api.o

graphic_server.o: gfx/graphic_server.c inc/graphic_server.h inc/graphic_api.h inc/gfx.h inc/mouse.h inc/kbd.h inc/pit.h inc/kstring.h
	$(CC) $(CFLAGS) -c gfx/graphic_server.c -o graphic_server.o

app_demo.o: core/app_demo.c inc/app_demo.h inc/graphic_server.h inc/asset.h inc/pit.h inc/kstdio.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/app_demo.c -o app_demo.o

myfsapp.o: core/myfsapp.c inc/myfsapp.h inc/graphic_server.h inc/graphic_api.h inc/asset.h inc/myfs.h inc/kstring.h inc/kstdio.h inc/pit.h
	$(CC) $(CFLAGS) -c core/myfsapp.c -o myfsapp.o

sched.o: core/sched.c inc/sched.h inc/pit.h inc/kstring.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/sched.c -o sched.o

sched_switch.o: core/sched_switch.asm
	$(NASM) -f elf64 core/sched_switch.asm -o sched_switch.o

fslock.o: fs/fslock.c inc/fslock.h inc/sched.h
	$(CC) $(CFLAGS) -c fs/fslock.c -o fslock.o

term.o: core/term.c inc/term.h inc/graphic_server.h inc/graphic_api.h inc/asset.h inc/syslib.h inc/sched.h inc/pit.h inc/kstring.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/term.c -o term.o

syscall.o: core/syscall.c inc/syscall.h inc/myfs.h inc/fslock.h inc/sched.h inc/pit.h inc/rtc.h inc/kstring.h
	$(CC) $(CFLAGS) -c core/syscall.c -o syscall.o

syslib.o: core/syslib.c inc/syslib.h inc/syscall.h
	$(CC) $(CFLAGS) -c core/syslib.c -o syslib.o

config.o: fs/config.c inc/config.h inc/syslib.h inc/asset.h inc/kstring.h inc/kstdio.h inc/kstdlib.h
	$(CC) $(CFLAGS) -c fs/config.c -o config.o

kernel.o: core/kernel.c inc/kstdio.h inc/memory.h inc/syscall.h
	$(CC) $(CFLAGS) -c core/kernel.c -o kernel.o

syscall_entry.o: core/syscall_entry.asm
	nasm -f elf64 core/syscall_entry.asm -o syscall_entry.o

app.elf: app/hello_app.c app/myos_app.h app/app.ld
	$(CC) $(CFLAGS) -c app/hello_app.c -o hello_app.o
	ld.lld -T app/app.ld -nostdlib -static -o app.elf hello_app.o
astudio.o: app/astudio.c app/astudio.h
studio_app.o: app/studio_app.c app/astudio.h
studio.elf: studio_app.o astudio.o
	ld.lld -T app/app.ld -nostdlib -static -o studio.elf studio_app.o astudio.o
studio_blob.c: studio.elf
	python3 mkblob.py studio.elf studio_blob.c studio_blob
gui_app.o: app/gui_app.c app/astudio.h inc/ga_remote.h
gui.elf: gui_app.o astudio.o
	ld.lld -T app/app.ld -nostdlib -static -o gui.elf gui_app.o astudio.o
gui_blob.c: gui.elf
	python3 mkblob.py gui.elf gui_blob.c gui_blob

app_blob.c: app.elf mkblob.py
	python3 mkblob.py app.elf app_blob.c app_blob

app_blob.o: app_blob.c
	$(CC) $(CFLAGS) -c app_blob.c -o app_blob.o

elfload.o: core/elfload.c inc/elfload.h inc/syslib.h inc/syscall.h inc/sched.h inc/memory.h inc/kstring.h inc/kstdio.h
	$(CC) $(CFLAGS) -c core/elfload.c -o elfload.o

astudio.o: app/astudio.c app/astudio.h inc/ga_remote.h
	$(CC) $(CFLAGS) -c app/astudio.c -o astudio.o
studio_app.o: app/studio_app.c app/astudio.h
	$(CC) $(CFLAGS) -c app/studio_app.c -o studio_app.o
gui_app.o: app/gui_app.c app/astudio.h inc/ga_remote.h
	$(CC) $(CFLAGS) -c app/gui_app.c -o gui_app.o

dma.o: mm/dma.c inc/dma.h inc/memory.h inc/kstdio.h
	$(CC) $(CFLAGS) -c mm/dma.c -o dma.o
osdisk.o: fs/osdisk.c inc/osdisk.h inc/bdev.h inc/kstdio.h
	$(CC) $(CFLAGS) -c fs/osdisk.c -o osdisk.o
ga_remote.o: gfx/ga_remote.c inc/ga_remote.h inc/graphic_server.h inc/graphic_api.h
	$(CC) $(CFLAGS) -c gfx/ga_remote.c -o ga_remote.o
fblock.o: gfx/fblock.c inc/fblock.h inc/sched.h
	$(CC) $(CFLAGS) -c gfx/fblock.c -o fblock.o
sxnl.o: sec/sxnl.c inc/sxnl.h inc/kstdio.h inc/sched.h inc/pit.h
	$(CC) $(CFLAGS) -c sec/sxnl.c -o sxnl.o
