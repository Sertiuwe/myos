import sys

path = "/storage/emulated/0/Download/ARM4.cpp"
with open(path) as f:
    src = f.read()

orig_len = len(src)

marker = "// Определение глобального указателя"
idx = src.find(marker)
if idx < 0:
    print("[!] не нашёл маркер 'Определение глобального указателя'")
    sys.exit(1)

line_start = src.rfind("// ============================================================", 0, idx)
if line_start < 0:
    line_start = idx

sdcard_class = '''// ============================================================
// SDCard — виртуальная SD-карта (backed by host file)
// ============================================================
class SDCard {
public:
    uint32_t base;
    std::string name;
    FILE* fp = nullptr;
    std::string path;
    uint32_t total_blocks = 0;
    uint32_t cmd = 0;
    uint32_t lba = 0;
    uint32_t count = 1;
    uint32_t offset = 0;
    uint32_t error = 0;
    uint32_t status = 1;
    uint8_t  block_buf[512];
    bool     dirty = false;
    uint64_t ops = 0;

    static constexpr uint32_t CMD_READ  = 1;
    static constexpr uint32_t CMD_WRITE = 2;
    static constexpr uint32_t CMD_FLUSH = 3;

    SDCard(uint32_t b, const char* n) : base(b), name(n) {}
    ~SDCard() { if (fp) std::fclose(fp); }

    bool contains(uint32_t addr) const { return addr >= base && addr < base + 0x100; }

    bool open_or_create(const char* p, uint32_t num_blocks) {
        path = p;
        total_blocks = num_blocks;
        fp = std::fopen(p, "r+b");
        if (!fp) {
            fp = std::fopen(p, "w+b");
            if (!fp) return false;
            std::vector<uint8_t> zero(512, 0);
            for (uint32_t i = 0; i < num_blocks; i++)
                std::fwrite(zero.data(), 1, 512, fp);
            std::fflush(fp);
            std::printf("[SD] created %s (%u KB)\\n", p, num_blocks / 2);
        } else {
            std::printf("[SD] opened %s\\n", p);
        }
        std::memset(block_buf, 0, sizeof(block_buf));
        return true;
    }

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
            case 0x00: v = status; break;
            case 0x04: v = cmd; break;
            case 0x08: v = lba; break;
            case 0x0C: v = count; break;
            case 0x10: {
                uint32_t o = offset & ~3u;
                if (o < 512) v = *(uint32_t*)(block_buf + o);
                offset += 4;
                break;
            }
            case 0x14: v = offset; break;
            case 0x18: v = total_blocks; break;
            case 0x1C: v = error; break;
            default:   v = 0;
        }
        if (width == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFF;
        else if (width == 1) v = (v >> ((addr & 3) * 8)) & 0xFF;
        return v;
    }

    void write(uint32_t addr, uint32_t val, int width) {
        uint32_t off = addr - base;
        switch (off & ~3u) {
            case 0x04: cmd = val & 0xFF; exec_cmd(); break;
            case 0x08: lba = val; break;
            case 0x0C: count = val; break;
            case 0x10: {
                uint32_t o = offset & ~3u;
                if (o < 512) { *(uint32_t*)(block_buf + o) = val; dirty = true; }
                offset += 4;
                break;
            }
            case 0x14: offset = val; break;
            case 0x1C: error = 0; break;
            default: (void)width; break;
        }
    }

    void exec_cmd() {
        error = 0;
        ops++;
        if (!fp) { error = 1; return; }
        switch (cmd) {
            case CMD_READ:
                if (lba >= total_blocks) { error = 2; return; }
                std::fseek(fp, (long)lba * 512, SEEK_SET);
                std::fread(block_buf, 1, 512, fp);
                offset = 0;
                break;
            case CMD_WRITE:
                if (lba >= total_blocks) { error = 2; return; }
                std::fseek(fp, (long)lba * 512, SEEK_SET);
                std::fwrite(block_buf, 1, 512, fp);
                std::fflush(fp);
                dirty = false;
                offset = 0;
                break;
            case CMD_FLUSH:
                std::fflush(fp);
                break;
            default:
                error = 3;
        }
    }

    void dump() const {
        std::printf("--- SDCard @ 0x%08X (%s) ---\\n", base, name.c_str());
        std::printf("  path:         %s\\n", path.empty() ? "(none)" : path.c_str());
        std::printf("  total blocks: %u (%u KB)\\n", total_blocks, total_blocks / 2);
        std::printf("  status:       0x%X\\n", status);
        std::printf("  lba/offset:   %u / %u\\n", lba, offset);
        std::printf("  cmd:          %u\\n", cmd);
        std::printf("  error:        %u\\n", error);
        std::printf("  total ops:    %llu\\n", (unsigned long long)ops);
    }
};

'''

src = src[:line_start] + sdcard_class + src[line_start:]
print("[1] SDCard class added")

old = '    PL190_VIC  vic{0x10140000, "VIC"};'
new = '    PL190_VIC  vic{0x10140000, "VIC"};\n    SDCard     sd{0x10100000, "SD"};'
if old in src:
    src = src.replace(old, new, 1)
    print("[2] sd field added")
else:
    print("[2] ERROR: vic field not found")

patches_r = [
    ("        if (emu->vic.contains(a))    return emu->vic.read(a, 4);",
     "        if (emu->sd.contains(a))     return emu->sd.read(a, 4);\n        if (emu->vic.contains(a))    return emu->vic.read(a, 4);"),
    ("        if (emu->vic.contains(a))    return (uint16_t)emu->vic.read(a, 2);",
     "        if (emu->sd.contains(a))     return (uint16_t)emu->sd.read(a, 2);\n        if (emu->vic.contains(a))    return (uint16_t)emu->vic.read(a, 2);"),
    ("        if (emu->vic.contains(a))    return (uint8_t)emu->vic.read(a, 1);",
     "        if (emu->sd.contains(a))     return (uint8_t)emu->sd.read(a, 1);\n        if (emu->vic.contains(a))    return (uint8_t)emu->vic.read(a, 1);"),
]
for i, (o, n) in enumerate(patches_r, 3):
    if o in src:
        src = src.replace(o, n, 1)
        print(f"[{i}] read patch OK")
    else:
        print(f"[{i}] ERROR")

patches_w = [
    ("        if (emu->vic.contains(a))    { emu->vic.write(a, v, 4); return; }",
     "        if (emu->sd.contains(a))     { emu->sd.write(a, v, 4); return; }\n        if (emu->vic.contains(a))    { emu->vic.write(a, v, 4); return; }"),
    ("        if (emu->vic.contains(a))    { emu->vic.write(a, v, 2); return; }",
     "        if (emu->sd.contains(a))     { emu->sd.write(a, v, 2); return; }\n        if (emu->vic.contains(a))    { emu->vic.write(a, v, 2); return; }"),
    ("        if (emu->vic.contains(a))    { emu->vic.write(a, v, 1); return; }",
     "        if (emu->sd.contains(a))     { emu->sd.write(a, v, 1); return; }\n        if (emu->vic.contains(a))    { emu->vic.write(a, v, 1); return; }"),
]
for i, (o, n) in enumerate(patches_w, 6):
    if o in src:
        src = src.replace(o, n, 1)
        print(f"[{i}] write patch OK")
    else:
        print(f"[{i}] ERROR")

old = '''        if (std::strcmp(cmd, "vic") == 0) {
            emu.vic.dump();
            return false;
        }'''
new = '''        if (std::strcmp(cmd, "vic") == 0) {
            emu.vic.dump();
            return false;
        }
        if (std::strcmp(cmd, "sd") == 0) {
            emu.sd.dump();
            return false;
        }'''
if old in src:
    src = src.replace(old, new, 1)
    print("[9] sd command added")
else:
    print("[9] ERROR")

old = '''    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--test") == 0) return run_tests();
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) { cfg_path = argv[++i]; continue; }
        if (std::strcmp(argv[i], "--no-config") == 0) { no_config = true; continue; }
        if (argv[i][0] == '-') { std::printf("Неизвестный параметр: %s\\n", argv[i]); return 1; }
    }'''
new = '''    const char* sd_path = "myos.ssd";
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--test") == 0) return run_tests();
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) { cfg_path = argv[++i]; continue; }
        if (std::strcmp(argv[i], "--no-config") == 0) { no_config = true; continue; }
        if (std::strcmp(argv[i], "--sd") == 0 && i + 1 < argc) { sd_path = argv[++i]; continue; }
        if (argv[i][0] == '-') { std::printf("Неизвестный параметр: %s\\n", argv[i]); return 1; }
    }
    emu.sd.open_or_create(sd_path, 2048);'''
if old in src:
    src = src.replace(old, new, 1)
    print("[10] --sd CLI added")
else:
    print("[10] ERROR")

with open(path, "w") as f:
    f.write(src)

print(f"PATCH COMPLETE (was {orig_len}, now {len(src)}, delta {len(src)-orig_len})")
