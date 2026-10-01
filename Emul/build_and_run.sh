#!/data/data/com.termux/files/usr/bin/bash
cd ~/myos

echo "[1] Копируем исходник из Downloads..."
cp ~/storage/shared/Download/ARM4.cpp ./ARM4.cpp

echo "[2] Применяем хирургический патч (добавляем setmem)..."
python3 - <<'PYEOF'
import sys

filename = 'ARM4.cpp'
with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

marker = 'if (std::strcmp(cmd, "mem") == 0) {'
new_code = '''        if (std::strcmp(cmd, "setmem") == 0) {
            if (!arg1[0] || !arg2[0]) { std::printf("Использование: setmem <addr> <value>\\n"); return false; }
            uint32_t addr = parse_u32(arg1);
            uint32_t val = parse_u32(arg2);
            emu.mem.write32(addr & ~3u, val);
            std::printf("[MEM] 0x%08X <- 0x%08X\\n", addr & ~3u, val);
            return false;
        }
        '''

if marker in content and 'setmem' not in content:
    content = content.replace(marker, new_code + marker)
    with open(filename, 'w') as f:
        f.write(content)
    print('[УСПЕХ] Патч успешно применен!')
elif 'setmem' in content:
    print('[ИНФО] Команда setmem уже есть в коде.')
else:
    print('[ОШИБКА] Маркер не найден в ARM4.cpp.')
    sys.exit(1)
PYEOF

if [ $? -ne 0 ]; then
    echo "Ошибка патча."
    exit 1
fi

echo "[3] Компилируем с жесткой оптимизацией..."
clang++ -O3 -std=c++17 ARM4.cpp -o arm4emu

if [ $? -eq 0 ]; then
    echo "[ГОТОВО] Бинарник собран. Запускаем эмулятор с твоей ОС..."
    # Передаем myos.bin как аргумент, чтобы эмулятор сразу его загрузил
    ./arm4emu myos.bin
else
    echo "[Пиздец] Ошибка компиляции. Скидывай вывод clang++."
fi
