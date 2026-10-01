import pathlib
p = pathlib.Path("mmu.h")
src = p.read_text()

# 1) В translate — устанавливать fsr_code для невалидного/неизвестного дескриптора
old = '''        // Fault
    fail:
        faults++; trans_faults++;'''
new = '''        // Fault
    fail:
        if (fsr_code == 0) fsr_code = MMU_FSR_TRANS;   // invalid descriptor
        faults++; trans_faults++;'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] default fsr_code = TRANS")
else:
    print("[!] fail не найден")

# 2) Domain dump — добавить "mg" для domain=3 (manager)
old = '''            const char* s = (d==0)?"no":(d==1)?"cl":(d==2)?"rs":"??";'''
new = '''            const char* s = (d==0)?"no":(d==1)?"cl":(d==2)?"rs":(d==3)?"mg":"??";'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] Domain dump: mg добавлен")
else:
    print("[!] dump не найден")

p.write_text(src)
