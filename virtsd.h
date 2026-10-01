// virtsd.h — Виртуальная SD-карта для ARM4
//   * Block device 512 байт
//   * MMIO 0x10100000 (см. sdcard.h ядра)
//   * Образ: myos.ssd (MyFS, 1 MB)
#ifndef ARM4_VIRTSD_H
#define ARM4_VIRTSD_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <fstream>

class VirtSD {
public:
    enum : uint32_t {
        SD_STATUS = 0x00,   // bit0 = present
        SD_CMD    = 0x04,   // 1=read, 2=write, 3=flush
        SD_LBA    = 0x08,
        SD_COUNT  = 0x0C,
        SD_DATA   = 0x10,   // 32-битный порт данных
        SD_OFFSET = 0x14,   // курсор в block_buf
        SD_SIZE   = 0x18,   // всего блоков
        SD_ERROR  = 0x1C,   // 0 = ok
    };
    enum : uint32_t { CMD_READ = 1, CMD_WRITE = 2, CMD_FLUSH = 3 };

    static constexpr uint32_t BLOCK_SIZE = 512;

    uint32_t    base;
    const char* name;

    std::unique_ptr<std::fstream> fp;
    uint8_t      block_buf[BLOCK_SIZE] = {0};
    uint32_t     offset   = 0;
    uint32_t     lba      = 0;
    uint32_t     count    = 1;
    uint32_t     error    = 0;
    bool         present  = false;
    uint32_t     img_size = 0;

    VirtSD(uint32_t b, const char* n) : base(b), name(n) {
        open_image("myos.ssd");
    }

    bool contains(uint32_t a) const { return a >= base && a < base + 0x20u; }

    uint32_t total_blocks() const { return img_size / BLOCK_SIZE; }

    void open_image(const char* path) {
        fp = std::make_unique<std::fstream>();
        fp->open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!fp->is_open()) {
            fp.reset();
            present = false;
            std::printf("[SD]   Образ %s не найден (SD неактивна)\n", path);
            return;
        }
        fp->seekg(0, std::ios::end);
        img_size = (uint32_t)fp->tellg();
        fp->seekg(0, std::ios::beg);
        fp->clear();
        present = true;
        error = 0;
        std::printf("[SD]   Образ %s: %u байт, %u блоков\n",
                    path, img_size, total_blocks());
    }

    uint32_t read(uint32_t addr, int width_v) {
        uint32_t off = addr - base;
        uint32_t v = 0;

        switch (off & ~3u) {
        case SD_STATUS: v = present ? 1u : 0u; break;
        case SD_CMD:    v = 0; break;
        case SD_LBA:    v = lba; break;
        case SD_COUNT:  v = count; break;
        case SD_DATA:
            if (offset + 4 <= BLOCK_SIZE) {
                std::memcpy(&v, block_buf + offset, 4);
                offset += 4;
            } else { error = 1; v = 0xFFFFFFFFu; }
            break;
        case SD_OFFSET: v = offset; break;
        case SD_SIZE:   v = total_blocks(); break;
        case SD_ERROR:  v = error; break;
        default: v = 0;
        }

        if (width_v == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFFu;
        else if (width_v == 1) v = (v >> ((addr & 3) * 8)) & 0xFFu;
        return v;
    }

    void write(uint32_t addr, uint32_t v, int width_v) {
        (void)width_v;
        uint32_t off = addr - base;

        switch (off & ~3u) {
        case SD_STATUS: break;
        case SD_CMD:
            if (v == CMD_READ) {
                if (!present || !fp || !fp->is_open()) { error = 1; break; }
                fp->clear();
                fp->seekg((std::streamoff)lba * BLOCK_SIZE, std::ios::beg);
                fp->read((char*)block_buf, BLOCK_SIZE);
                error = fp->good() ? 0 : 2;
                offset = 0;
            } else if (v == CMD_WRITE) {
                if (!present || !fp || !fp->is_open()) { error = 1; break; }
                fp->clear();
                fp->seekp((std::streamoff)lba * BLOCK_SIZE, std::ios::beg);
                fp->write((const char*)block_buf, BLOCK_SIZE);
                fp->flush();
                error = fp->good() ? 0 : 2;
                offset = 0;
            } else if (v == CMD_FLUSH) {
                if (present && fp && fp->is_open()) fp->flush();
            }
            break;
        case SD_LBA:    lba = v; break;
        case SD_COUNT:  count = v; break;
        case SD_DATA:
            if (offset + 4 <= BLOCK_SIZE) {
                std::memcpy(block_buf + offset, &v, 4);
                offset += 4;
            } else { error = 1; }
            break;
        case SD_OFFSET: offset = v; break;
        case SD_SIZE:   break;
        case SD_ERROR:  error = v; break;
        default: break;
        }
    }

    void tick() {}
};

#endif
