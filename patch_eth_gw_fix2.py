import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def show(tag):
    print(f"[+] {tag}")

# ---------- eth gw ----------
old = '''            if (std::strcmp(arg1, "gw") == 0) {
                e.gateway_enabled = arg2[0] ? (std::atoi(arg2)!=0) : !e.gateway_enabled;
                std::printf("gateway_enabled=%d\\n", (int)e.gateway_enabled);
                return false;
            }'''
new = '''            if (std::strcmp(arg1, "gw") == 0) {
                if      (arg2[0] == 'o' && arg2[1] == 'n')  e.gateway_enabled = true;
                else if (arg2[0] == 'o' && arg2[1] == 'f')  e.gateway_enabled = false;
                else if (arg2[0] >= '0' && arg2[0] <= '9')  e.gateway_enabled = (std::atoi(arg2) != 0);
                else                                        e.gateway_enabled = !e.gateway_enabled;
                std::printf("gateway_enabled=%d\\n", (int)e.gateway_enabled);
                return false;
            }'''
if old in src:
    src = src.replace(old, new, 1); show("eth gw on/off")
else:
    print("[!] eth gw — шаблон всё ещё не найден"); ok = False

# ---------- остальные ----------
for cmd, field in [("loopback","loopback"), ("promisc","promisc"), ("sniff","sniff")]:
    old = f'''            if (std::strcmp(arg1, "{cmd}") == 0) {{
                e.{field} = arg2[0] ? (std::atoi(arg2)!=0) : !e.{field};
                std::printf("{cmd}=%d\\n", (int)e.{field});
                return false;
            }}'''
    new = f'''            if (std::strcmp(arg1, "{cmd}") == 0) {{
                if      (arg2[0] == 'o' && arg2[1] == 'n')  e.{field} = true;
                else if (arg2[0] == 'o' && arg2[1] == 'f')  e.{field} = false;
                else if (arg2[0] >= '0' && arg2[0] <= '9')  e.{field} = (std::atoi(arg2) != 0);
                else                                        e.{field} = !e.{field};
                std::printf("{cmd}=%d\\n", (int)e.{field});
                return false;
            }}'''
    if old in src:
        src = src.replace(old, new, 1); show(f"eth {cmd} on/off")
    else:
        print(f"[!] eth {cmd} — шаблон не найден"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
