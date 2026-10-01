#!/usr/bin/env python3
"""
pack_hex.py — упаковка EP32cm .hex в образ myos.ssd

Формат .hex: ASCII-строка hex-символов, кодирующая слова по 4 байта БОЛЬШИМ
порядком (BE). VMCU читает память как little-endian, поэтому переставляем
байты внутри каждой 4-байтной группы.

Использование:
  python3 tools/pack_hex.py <input.hex> <image.ssd> <lba>
"""
import sys
import os

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input.hex> <image.ssd> <lba>")
        sys.exit(1)

    hex_path = sys.argv[1]
    img_path = sys.argv[2]
    lba      = int(sys.argv[3], 0)

    with open(hex_path, 'r') as f:
        raw = f.read()

    hexstr = ''.join(c for c in raw if c in '0123456789abcdefABCDEF')

    if len(hexstr) % 8 != 0:
        print(f"[!] Длина hex-строки не кратна 8: {len(hexstr)} (нужно для 32-битных слов)")
        sys.exit(1)

    # Разбиваем на 8-символьные группы = 4 байта каждая.
    # Каждая группа — BE-слово. Разворачиваем байты → LE.
    code = bytearray()
    for i in range(0, len(hexstr), 8):
        word_be = bytes.fromhex(hexstr[i:i+8])
        code += word_be[::-1]           # BE → LE

    print(f"[+] {hex_path}: {len(code)} байт кода (byte-swapped 32-битных слов)")

    if not os.path.exists(img_path):
        print(f"[!] {img_path} не существует")
        sys.exit(1)

    img_size = os.path.getsize(img_path)
    offset   = lba * 512
    end      = offset + len(code)

    if end > img_size:
        print(f"[!] Не влезает: нужно до {end}, в образе {img_size}")
        sys.exit(1)

    with open(img_path, 'r+b') as f:
        f.seek(offset)
        f.write(code)

    blocks_used = (len(code) + 511) // 512
    print(f"[+] Записано {len(code)} байт в {img_path} @ LBA {lba} (блок {blocks_used})")

if __name__ == '__main__':
    main()
