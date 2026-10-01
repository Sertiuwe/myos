import sys

filename = 'emul'
try:
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
except FileNotFoundError:
    print("[ОШИБКА] Файл 'emul' не найден в текущей папке!")
    sys.exit(1)

# Ищем место перед командой 'mem' и вставляем туда 'setmem'
marker = 'if (std::strcmp(cmd, "mem") == 0) {'
new_code = """        if (std::strcmp(cmd, "setmem") == 0) {
            if (!arg1[0] || !arg2[0]) { std::printf("Использование: setmem <addr> <value>\\n"); return false; }
            uint32_t addr = parse_u32(arg1);
            uint32_t val = parse_u32(arg2);
            emu.mem.write32(addr & ~3u, val);
            std::printf("[MEM] 0x%08X <- 0x%08X\\n", addr & ~3u, val);
            return false;
        }
        """

if marker in content:
    content = content.replace(marker, new_code + marker)
    with open(filename, 'w') as f:
        f.write(content)
    print("[УСПЕХ] Патч успешно применен к файлу 'emul'!")
elif "setmem" in content:
    print("[ИНФО] Команда 'setmem' уже присутствует в коде. Патч не нужен.")
else:
    print("[ОШИБКА] Маркер для вставки не найден. Структура файла отличается.")
    sys.exit(1)
