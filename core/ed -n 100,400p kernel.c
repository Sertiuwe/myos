[33ma6bfd4e[m[33m ([m[1;36mHEAD[m[33m -> [m[1;32mmaster[m[33m, [m[1;31morigin/master[m[33m)[m ata: integrate bus-master DMA into read path with PIO fallback; dmastat counters (19 DMA reads, 0 fallbacks on ELF load)
[33m0b5c7f3[m dma: PCI IDE bus-mastering, PRDT with 64K splitting, ATA READ DMA + PIO reference bench (25x faster, byte-identical)
[33mbb0b30e[m dma: correct dma_lib over identity-mapped heap (alloc/free/virt_to_phys/selftest); remove broken linear DMA pool
[33m3a9cb49[m cleanup: remove obsolete syscall_entry.S (superseded by .asm)
[33mfe46031[m elf loader: load .elf apps from myfs, resolve imports via kernel export table, run in Ring 3, return exit code; fix myfs inode layout + dirent management; syslib over int 0x80; SYS_EXIT + process lifecycle
[33mf5d6bd9[m docs: expand README without backticks
[33mfcf4d5e[m docs: add MIT license
[33mf4dc3cf[m docs: add initial README.md
[33m3f9cf92[m Add graphical server, scheduler, syslib and applications
[33mcbd749d[m graphic_api: canvas primitives, clip stack, runtime font probing, widgets, icons; asset theme/metrics/strings; gfx_flush_rect
[33m51d35c7[m PS/2 mouse: proper i8042 command byte (IRQ12 armed), atomic byte intake, overflow discard, kbd aux routing, cursor rendering, mouse stats command
[33m96c77d6[m PCI: bus 0 enumeration, config space read/write, lspci command, device database
[33m8ebc7e1[m myfs: mkdir, rename, writeback cache, ls -l with size/mtime, sync, persistent subdirs
[33m8601749[m PIT+PIC: 100Hz timer, IRQ0/IRQ1 handlers, sleep_ms(), uptime command
[33m4c3266d[m IDT: 256-entry table, exception handlers 0-31 with full register dump, dynamic CS detection
[33m1c83bf5[m myfs: custom Unix-like FS with superblock/bitmap/inodes/dirs, persistent across reboot
[33mca1b973[m RTC driver + block cache: date cmd, bcache hit/miss, blk via cache
[33mb191893[m ATA: LBA48, model/serial from IDENTIFY, post-transfer ERR check, per-device stats, atastat cmd
[33m1406246[m ATA PIO driver complete: SRST reset, strict IDENTIFY, persistent read/write verified
[33m89b75b7[m libk: kprintf/kstring/kstdlib/kpanic, zero .bss on entry, sysinfo command
