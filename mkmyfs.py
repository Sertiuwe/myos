#!/usr/bin/env python3
"""Создаёт пустой образ myfs на fs.img. Layout строго соответствует myfs_inode_t:
   mode(u32) size(u32) nblocks(u16) direct[10](u16) indirect(u16) mtime(u32) = 36 bytes
"""
import struct, sys

BLK = 512
INODE_SIZE = 64
INODES_PER_BLK = BLK // INODE_SIZE
MAGIC = b'MYFS1\x00\x00\x00'

TOTAL_BLOCKS = 16384
INODE_COUNT  = 256
BM_BLOCKS    = (TOTAL_BLOCKS + 7) // 8 // BLK
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
            MAGIC,
            BLK,
            TOTAL_BLOCKS,
            BM_BLOCKS,
            BM_BLOCKS,
            1 + BM_BLOCKS,
            ITAB_BLOCKS,
            DATA_START)
        sb += struct.pack('<HI', INODE_COUNT, 2)  # root_ino=2
        write_block(f, 0, sb)

        # --- Bitmap блоков (блоки 1..BM_BLOCKS) ---
        bm = bytearray(BM_BLOCKS * BLK)
        for b in range(DATA_START + 1):
            bm[b // 8] |= (1 << (b % 8))
        for i in range(BM_BLOCKS):
            write_block(f, 1 + i, bytes(bm[i*BLK:(i+1)*BLK]))

        # --- Таблица инод (блоки 1+BM_BLOCKS .. ) ---
        inodes = bytearray(ITAB_BLOCKS * BLK)
        root_off = 2 * INODE_SIZE
        mode = 0o40755

        # ПРАВИЛЬНЫЙ формат: IIH10HHI = 36 байт, строго соответствует myfs_inode_t
        struct.pack_into('<IIH10HHI', inodes, root_off,
            mode,              # uint32_t mode
            0,                 # uint32_t size  (пустой каталог)
            1,                 # uint16_t nblocks
            DATA_START, 0, 0, 0, 0, 0, 0, 0, 0, 0,   # uint16_t direct[10]
            0,                 # uint16_t indirect
            0                  # uint32_t mtime
        )
        for i in range(ITAB_BLOCKS):
            write_block(f, 1 + BM_BLOCKS + i, bytes(inodes[i*BLK:(i+1)*BLK]))

        # --- Корневой каталог (блок DATA_START) ---
        write_block(f, DATA_START, b'')

        print(f"myfs created: {img}")
        print(f"  total={TOTAL_BLOCKS} blocks ({TOTAL_BLOCKS*BLK//1024} KiB)")
        print(f"  bitmap={BM_BLOCKS} blks, inodes={INODE_COUNT} ({ITAB_BLOCKS} blks)")
        print(f"  data starts at block {DATA_START}")
        print(f"  root inode=2, root dir block={DATA_START}")
        print(f"  inode size on disk = {struct.calcsize('<IIH10HHI')} bytes (must be 36)")

if __name__ == '__main__':
    main()
