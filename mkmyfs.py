#!/usr/bin/env python3
"""Создаёт пустой образ myfs на fs.img"""
import struct, sys

BLK = 512
INODE_SIZE = 64
INODES_PER_BLK = BLK // INODE_SIZE
MAGIC = b'MYFS1\x00\x00\x00'

# Параметры образа
TOTAL_BLOCKS = 16384       # 8 MiB / 512
INODE_COUNT  = 256
BM_BLOCKS    = (TOTAL_BLOCKS + 7) // 8 // BLK  # битмап блоков
ITAB_BLOCKS  = (INODE_COUNT * INODE_SIZE + BLK - 1) // BLK
DATA_START   = 1 + BM_BLOCKS + ITAB_BLOCKS

def write_block(f, lba, data):
    f.seek(lba * BLK)
    f.write(data.ljust(BLK, b'\x00'))

def main():
    img = sys.argv[1] if len(sys.argv) > 1 else 'fs.img'
    with open(img, 'r+b') as f:
        # --- Superblock (блок 0) ---
        sb = struct.pack('<8sIIHHHHI',
            MAGIC,           # magic
            BLK,             # block_size
            TOTAL_BLOCKS,    # total_blocks
            BM_BLOCKS,       # bm_start (всегда 1), но храним длину
            BM_BLOCKS,       # bm_len
            1 + BM_BLOCKS,   # itab_start
            ITAB_BLOCKS,     # itab_len
            DATA_START       # data_start
        )
        # Добавляем inode_count и root_ino
        sb += struct.pack('<HI', INODE_COUNT, 2)  # root_ino=2 (0=free, 1=reserved)
        write_block(f, 0, sb)

        # --- Bitmap блоков (блоки 1..BM_BLOCKS) ---
        # Помечаем занятые: sb(0) + bm + itab + root dir (data_start)
        bm = bytearray(BM_BLOCKS * BLK)
        for b in range(DATA_START + 1):
            bm[b // 8] |= (1 << (b % 8))
        for i in range(BM_BLOCKS):
            write_block(f, 1 + i, bytes(bm[i*BLK:(i+1)*BLK]))

        # --- Таблица инод (блоки 1+BM_BLOCKS .. ) ---
        # Inode 2 = корневой каталог
        inodes = bytearray(ITAB_BLOCKS * BLK)
        # mode=0o40755 (dir), size=0, nblocks=1, direct[0]=DATA_START, mtime из RTC позже
        root_off = 2 * INODE_SIZE
        mode = 0o40755
        struct.pack_into('<III10HHI', inodes, root_off,
            mode, 0, 1,                    # mode, size, nblocks
            DATA_START, 0,0,0,0,0,0,0,0,0,  # direct[10]
            0,                             # indirect
            0                              # mtime (обновится при mount)
        )
        for i in range(ITAB_BLOCKS):
            write_block(f, 1 + BM_BLOCKS + i, bytes(inodes[i*BLK:(i+1)*BLK]))

        # --- Корневой каталог (блок DATA_START) ---
        # Пустой каталог: просто нули (нет записей)
        write_block(f, DATA_START, b'')

    print(f"myfs created: {img}")
    print(f"  total={TOTAL_BLOCKS} blocks ({TOTAL_BLOCKS*BLK//1024} KiB)")
    print(f"  bitmap={BM_BLOCKS} blks, inodes={INODE_COUNT} ({ITAB_BLOCKS} blks)")
    print(f"  data starts at block {DATA_START}")
    print(f"  root inode=2, root dir block={DATA_START}")

if __name__ == '__main__':
    main()
