import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()
patched = 0

old16 = "    return icache.read16(a);\n}"
new16 = """    if (a >= 0x70000 && a < 0x71000) {
        uint16_t v; std::memcpy(&v, &data[a], 2); return v;
    }
    return icache.read16(a);
}"""
if 'return icache.read16(a);\n    return icache.read32' not in src \
   and 'if (a >= 0x70000 && a < 0x71000) {\n        uint16_t v; std::memcpy(&v, &data[a], 2); return v;\n    }\n    return icache.read16(a);' not in src:
    n = src.count(old16)
    if n == 1:
        src = src.replace(old16, new16, 1)
        print("[+] read16_ic bypass")
        patched += 1
    else:
        print(f"[!] read16_ic: {n} совпадений")
else:
    print("[=] read16_ic уже с bypass")

old32 = "    return icache.read32(a);\n}"
new32 = """    if (a >= 0x70000 && a < 0x71000) {
        uint32_t v; std::memcpy(&v, &data[a], 4); return v;
    }
    return icache.read32(a);
}"""
if 'if (a >= 0x70000 && a < 0x71000) {\n        uint32_t v; std::memcpy(&v, &data[a], 4); return v;\n    }\n    return icache.read32(a);' not in src:
    n = src.count(old32)
    if n == 1:
        src = src.replace(old32, new32, 1)
        print("[+] read32_ic bypass")
        patched += 1
    else:
        print(f"[!] read32_ic: {n} совпадений")
else:
    print("[=] read32_ic уже с bypass")

p.write_text(src)
print(f"ИТОГО: {patched}")
