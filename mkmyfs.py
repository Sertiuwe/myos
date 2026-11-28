#!/usr/bin/env python3
"""Format a myfs volume, optionally at an absolute LBA offset inside an image.

Inode layout matches myfs_inode_t exactly (36 bytes):
  mode(u32) size(u32) nblocks(u16) direct[10](u16) indirect(u16) mtime(u32)
"""
import struct, sys, argparse

BLK = 512
INODE_SIZE = 64
MAGIC = b'MYFS1\x00\x00\x00'

def ceil_div(a, b):
    return (a + b - 1) // b

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('img', nargs='?', default='fs.img')
    ap.add_argument('--base', type=int, default=0,
                    help='absolute LBA where the volume starts')
    ap.add_argument('--blocks', type=int, default=16384,
                    help='volume size in 512-byte blocks')
    ap.add_argument('--inodes', type=int, default=256)
    a = ap.parse_args()

    total = a.blocks
    bm_blocks   = ceil_div(ceil_div(total, 8), BLK)      # proper ceiling
    itab_blocks = ceil_div(a.inodes * INODE_SIZE, BLK)
    data_start  = 1 + bm_blocks + itab_blocks
    if data_start >= total:
        sys.exit("volume too small for metadata")

    with open(a.img, 'r+b') as f:
        f.seek(0, 2)
        size = f.tell()
        need = (a.base + total) * BLK
        if size < need:
            sys.exit(f"image too small: have {size} bytes, need {need}")

        def wblk(rel, data):
            f.seek((a.base + rel) * BLK)
            f.write(data.ljust(BLK, b'\x00'))

        sb = struct.pack('<8sIIHHHHI', MAGIC, BLK, total,
                         bm_blocks, bm_blocks, 1 + bm_blocks,
                         itab_blocks, data_start)
        sb += struct.pack('<HI', a.inodes, 2)            # root_ino = 2
        wblk(0, sb)

        bm = bytearray(bm_blocks * BLK)
        for b in range(data_start + 1):                  # metadata + root dir
            bm[b // 8] |= (1 << (b % 8))
        for i in range(bm_blocks):
            wblk(1 + i, bytes(bm[i*BLK:(i+1)*BLK]))

        itab = bytearray(itab_blocks * BLK)
        struct.pack_into('<IIH10HHI', itab, 2 * INODE_SIZE,
                         0o40755, 0, 1,
                         data_start, 0,0,0,0,0,0,0,0,0,
                         0, 0)
        for i in range(itab_blocks):
            wblk(1 + bm_blocks + i, bytes(itab[i*BLK:(i+1)*BLK]))

        wblk(data_start, b'')                            # empty root dir

    print(f"myfs volume in {a.img}")
    print(f"  base LBA   = {a.base}")
    print(f"  blocks     = {total} ({total*BLK//1024} KiB)")
    print(f"  bitmap     = {bm_blocks} blks, inodes={a.inodes} ({itab_blocks} blks)")
    print(f"  data@      = {data_start}  (absolute LBA {a.base + data_start})")
    print(f"  inode size = {struct.calcsize('<IIH10HHI')} bytes")

if __name__ == '__main__':
    main()
