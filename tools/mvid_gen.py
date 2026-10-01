#!/usr/bin/env python3
"""mvid_gen.py — генератор MV01-видео для MyOS.

Формат:
  16-байт header: magic "MV01" + w u16 + h u16 + fps u8 + bpp u8 + pad u16 + frames u32
  Далее frames × (w*h*2) байт RGB565 little-endian.
"""
import struct, math, sys, os

W, H = 80, 60
FRAMES = 400
FPS = 30

def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def make_frame(i):
    """Кадр i: пульсирующий градиент + 2 квадрата, движущихся по кругу."""
    t = i / FRAMES
    ang = t * 2 * math.pi

    r1x = int((W - 16) * (0.5 + 0.4 * math.cos(ang)))
    r1y = int((H - 16) * (0.5 + 0.4 * math.sin(ang)))
    r2x = int((W - 12) * (0.5 + 0.4 * math.cos(-ang * 1.7)))
    r2y = int((H - 12) * (0.5 + 0.4 * math.sin(-ang * 1.7)))

    px = bytearray()
    for y in range(H):
        for x in range(W):
            r = (x * 255) // W
            g = (y * 255) // H
            b = int(128 + 127 * math.sin(ang + x * 0.1))

            if r1x <= x < r1x + 16 and r1y <= y < r1y + 16:
                c = rgb565(255, 64, 64)
            elif r2x <= x < r2x + 12 and r2y <= y < r2y + 12:
                c = rgb565(64, 224, 255)
            else:
                c = rgb565(r, g, b)
            px += struct.pack('<H', c)
    return bytes(px)

def build_mvid():
    data = bytearray()
    data += b'MV01'
    data += struct.pack('<HHBBH', W, H, FPS, 16, 0)
    data += struct.pack('<I', FRAMES)
    for i in range(FRAMES):
        data += make_frame(i)
    return bytes(data)

def write_sd(img_path, lba, blob):
    with open(img_path, 'r+b') as f:
        f.seek(lba * 512)
        f.write(blob)
        r = len(blob) % 512
        if r:
            f.write(b'\x00' * (512 - r))

if __name__ == '__main__':
    img = sys.argv[1] if len(sys.argv) > 1 else 'myos.ssd'
    lba = int(sys.argv[2], 0) if len(sys.argv) > 2 else 2000

    blob = build_mvid()
    blocks = (len(blob) + 511) // 512
    print(f"[+] MV01: {W}x{H}, {FRAMES} кадров, {FPS} fps, {len(blob)} байт")
    print(f"[+] Блоков: {blocks}, LBA {lba}..{lba + blocks - 1}")
    write_sd(img, lba, blob)
    print(f"[+] Записано в {img}")
