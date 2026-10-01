#!/data/data/com.termux/files/usr/bin/bash
# epc_build.sh <input.s> [input.hex]
# Собирает .s -> .hex и печатает готовую команду для эмулятора.
set -e
SRC="$1"
HEX="${2:-${SRC%.s}.hex}"
python3 epc_asm.py "$SRC" "$HEX"
echo
echo "Загрузить в VMCU:"
echo "  vmcu load 0 $(cat "$HEX")"
