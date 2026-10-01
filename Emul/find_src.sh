#!/data/data/com.termux/files/usr/bin/bash
echo "=== Поиск файлов с кодом эмулятора (*.cpp, *.cc, *.cxx) ==="
find ~/ -type f \( -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" \) 2>/dev/null | grep -v "termux/files/usr" | grep -v ".termux"

echo ""
echo "=== Поиск файлов с именем 'ARM4' или 'emul' ==="
find ~/ -type f \( -iname "*arm4*" -o -iname "*emul*" \) 2>/dev/null | grep -v "termux/files/usr" | grep -v ".termux"
