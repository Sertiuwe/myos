import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def fix(name):
    global src, ok
    old = f'''            if (std::strcmp(arg1, "{name}") == 0) {{
                e.{name} = arg2[0] ? (std::atoi(arg2)!=0) : !e.{name};
                std::printf("{name}=%d\\n", (int)e.{name});
                return false;
            }}'''
    new = f'''            if (std::strcmp(arg1, "{name}") == 0) {{
                if (arg2[0] == 'o' && arg2[1] == 'n')      e.{name} = true;
                else if (arg2[0] == 'o' && arg2[1] == 'f') e.{name} = false;
                else if (arg2[0] >= '0' && arg2[0] <= '9') e.{name} = (std::atoi(arg2) != 0);
                else                                       e.{name} = !e.{name};
                std::printf("{name}=%d\\n", (int)e.{name});
                return false;
            }}'''
    if old in src:
        src = src.replace(old, new, 1)
        print(f"[+] eth {name}: on/off исправлено")
    else:
        print(f"[!] eth {name}: шаблон не найден")
        ok = False

for name in ["gw", "loopback", "promisc", "sniff"]:
    fix(name)

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
