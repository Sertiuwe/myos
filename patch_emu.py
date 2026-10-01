import sys
import os

filename = 'main.cpp'
if not os.path.exists(filename):
    print(f"[ОШИБКА] Файл {filename} не найден в текущей директории!")
    sys.exit(1)

with open(filename, 'r') as f:
    content = f.read()

# Ищем уникальный маркер — конец блока команды 'setreg'
marker = '            std::printf("R%d = 0x%08X\\n", n, v);\n            return false;\n        }'

# Код, который мы внедряем (команда setmem)
new_code = """
        if (std::strcmp(cmd, "setmem") == 0) {
            if (!arg1[0] || !arg2[0]) { std::printf("Использование: setmem <addr> <value>\\n"); return false; }
            uint32_t addr = parse_u32(arg1);
            uint32_t val = parse_u32(arg2);
            emu.mem.write32(addr & ~3u, val);
            std::printf("[MEM] 0x%08X <- 0x%08X\\n", addr & ~3u, val);
            return false;
        }"""

if marker in content:
    content = content.replace(marker, marker + new_code)
    with open(filename, 'w') as f:
        f.write(content)
    print("[УСПЕХ] Патч успешно применен! Команда 'setmem' добавлена в shell.")
elif "setmem" in content:
    print("[ИНФО] Команда 'setmem' уже присутствует в коде. Патч не требуется.")
else:
    print("[ОШИБКА] Не найден маркер для вставки. Структура файла изменилась.")
    sys.exit(1)
