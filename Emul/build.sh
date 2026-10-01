#!/data/data/com.termux/files/usr/bin/bash
set -e
cd "$(dirname "$0")"
echo "[BUILD] ARM4 v18 — сборка эмулятора"
clang++ -std=c++17 -O2 -o emul ARM4.cpp -lm
echo "[BUILD] готово"
ls -la emul
