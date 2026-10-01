CC      = clang --target=arm-unknown-none-eabi
LD      = ld.lld
OBJCOPY = llvm-objcopy

# Обычные флаги
CFLAGS = -march=armv4t -marm -O1 -ffreestanding -fno-builtin -fno-common -fno-stack-protector -Wall -Wextra -Wno-unused-parameter

# Специальные флаги для fs.c — без оптимизации (иначе clang ломает записи в fs_entries)
CFLAGS_FS = -march=armv4t -marm -O0 -ffreestanding -fno-builtin -fno-common -fno-stack-protector -Wall -Wextra -Wno-unused-parameter

OBJS = boot.o kernel.o switch.o print.o memmap.o match.o panic.o selftest.o fs.o sdcard.o eth.o net.o lcd.o video.o libgcc_stubs.o

all: myos.bin

myos.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

myos.bin: myos.elf
	$(OBJCOPY) -O binary $< $@
	@ls -la $@

# fs.c — отдельное правило с -O0
fs.o: fs.c fs.h
	$(CC) $(CFLAGS_FS) -c fs.c -o fs.o

# Все остальные .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) myos.elf myos.bin

.PHONY: all clean
