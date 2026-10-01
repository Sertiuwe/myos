import pathlib
p = pathlib.Path("mmu.h")
src = p.read_text()

# 1) Убираем forward declaration class MMU (был неправильный синтаксис) и заменяем на обычный extern
old = '''extern class CP15* g_cp15_ptr;
extern class MMU*  g_mmu_ptr;'''
new = '''class CP15;
class MMU;
extern CP15* g_cp15_ptr;
extern MMU*  g_mmu_ptr;'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] forward declarations")

# 2) Убираем вызовы g_mmu_ptr->flush() из CP15::write — заменяем на флаг
old = '''            case 7:
                // cache/TLB maintenance — flush TLB при любом обращении
                if (g_mmu_ptr) g_mmu_ptr->flush();
                break;
            case 8:
                if (g_mmu_ptr) g_mmu_ptr->flush();
                break;'''
new = '''            case 7:
            case 8:
                tlb_dirty = true;
                break;'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] CP15 c7/c8 -> флаг tlb_dirty")

# 3) Добавляем поле tlb_dirty в CP15
old = '''    uint32_t cpuid = 0x4100F000;   // ARM7TDMI-ish
    uint32_t cache_id = 0x00000000;'''
new = '''    uint32_t cpuid = 0x4100F000;   // ARM7TDMI-ish
    uint32_t cache_id = 0x00000000;
    bool     tlb_dirty = false;'''
if 'bool     tlb_dirty' not in src:
    src = src.replace(old, new, 1)
    print("[+] поле tlb_dirty")

p.write_text(src)
