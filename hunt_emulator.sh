#!/data/data/com.termux/files/usr/bin/bash

OUT="$HOME/hunt_results.txt"
: > "$OUT"

log() {
    printf '%s\n' "$*" >> "$OUT"
}

log "[START] $(date)"
log ""

log "[1] HOME: поиск по именам файлов"
find "$HOME" -type f \
    \( \
        -iname '*arm4*' -o \
        -iname '*emul*' -o \
        -iname '*.cpp' -o \
        -iname '*.cc' -o \
        -iname '*.cxx' -o \
        -iname '*.c' -o \
        -iname '*.h' -o \
        -iname '*.hpp' -o \
        -iname '*.txt' -o \
        -iname '*.inl' -o \
        -iname '*.inc' -o \
        -iname '*.md' \
    \) \
    -not -path '*/.git/*' \
    -not -path '*/node_modules/*' \
    -not -path '*/.termux/*' \
    -not -name 'hunt_results.txt' \
    -not -name 'hunt_emulator.sh' \
    2>/dev/null | sort -u >> "$OUT"

log ""
log "[2] HOME: поиск по содержимому"
grep -rIilF \
    --exclude='hunt_results.txt' \
    --exclude='hunt_emulator.sh' \
    --exclude-dir=.git \
    --exclude-dir=node_modules \
    --exclude-dir=.termux \
    -e 'ARM4 + Thumb Emulator' \
    -e 'PL011_UART' \
    -e 'SP804_Timer' \
    -e 'PL190_VIC' \
    -e 'PortManager' \
    -e 'RomLoader' \
    -e 'arm4.cfg' \
    "$HOME" 2>/dev/null | sort -u >> "$OUT"

log ""
log "[3] Логи, история, Makefile"
for f in "$HOME/.bash_history" "$HOME/myos/build.log" "$HOME/myos/Makefile"; do
    if [ -f "$f" ]; then
        log "--- $f"
        grep -aE 'ARM4|emul|clang|g\+\+|\.cpp' "$f" 2>/dev/null | tail -n 200 >> "$OUT"
    fi
done

log ""
log "[4] Строки из бинарника ~/myos/emul"
if [ -f "$HOME/myos/emul" ]; then
    grep -a -o -E '[^[:space:]"]*ARM4[^[:space:]"]*' "$HOME/myos/emul" 2>/dev/null | sort -u >> "$OUT"
    grep -a -o -E '/[^[:space:]"]+\.(cpp|cc|cxx|c|h|hpp)' "$HOME/myos/emul" 2>/dev/null | sort -u >> "$OUT"
fi

if [ -d "$HOME/storage/shared" ]; then
    log ""
    log "[5] Shared storage: поиск по именам"
    find "$HOME/storage/shared" -type f \
        \( \
            -iname '*arm4*' -o \
            -iname '*emul*' -o \
            -iname '*.cpp' -o \
            -iname '*.cc' -o \
            -iname '*.cxx' -o \
            -iname '*.c' -o \
            -iname '*.h' -o \
            -iname '*.hpp' -o \
            -iname '*.txt' \
        \) \
        -not -path '*/Android/data/*' \
        -not -path '*/Android/obb/*' \
        2>/dev/null | sort -u >> "$OUT"

    log ""
    log "[6] Shared storage: поиск по содержимому"
    grep -rIilF \
        --exclude-dir=Android \
        -e 'ARM4 + Thumb Emulator' \
        -e 'PL011_UART' \
        -e 'SP804_Timer' \
        -e 'PL190_VIC' \
        -e 'PortManager' \
        "$HOME/storage/shared" 2>/dev/null | sort -u >> "$OUT"
else
    log ""
    log "[5] Shared storage не найден. Сначала выполни: termux-setup-storage"
fi

log ""
log "[DONE]"
