#!/data/data/com.termux/files/usr/bin/bash
set -e
cd "$(dirname "$0")"

mkdir -p build

echo "[1/2] Сборка эмулятора ARM4..."
clang++ -O3 -std=c++17 ARM4.cpp -o build/arm4emu

echo "[2/2] Запуск эмулятора с myos.bin..."
exec ./build/arm4emu myos.bin
