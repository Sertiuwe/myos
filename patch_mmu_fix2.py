import pathlib
p = pathlib.Path("mmu.h")
src = p.read_text()

old = '''    uint32_t translate(uint32_t va, bool write, bool user, bool fetch,
                       bool& fault, int& fsr_code, CP15& cp15)
    {
        fault = false;
        fsr_code = 0;
        translations++;
'''
new = '''    uint32_t translate(uint32_t va, bool write, bool user, bool fetch,
                       bool& fault, int& fsr_code, CP15& cp15)
    {
        fault = false;
        fsr_code = 0;
        if (cp15.tlb_dirty) { flush(); cp15.tlb_dirty = false; }
        translations++;
'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] translate: проверка tlb_dirty")
else:
    print("[!] не найден translate")
p.write_text(src)
