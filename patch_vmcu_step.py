import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "step") == 0) {
                int n = arg2[0] ? std::atoi(arg2) : 1;
                for (int i = 0; i < n && v.running && !v.halted; i++) v.step();
                v.dump();
                return false;
            }'''

new = '''            if (std::strcmp(arg1, "step") == 0) {
                int n = arg2[0] ? std::atoi(arg2) : 1;
                bool saved = v.running;
                v.running = true;
                for (int i = 0; i < n && !v.halted; i++) v.step();
                v.running = saved;
                v.dump();
                return false;
            }'''

if old not in src:
    print("[!] шаблон не найден — уже пропатчено?")
    raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] vmcu step: single-step работает без run")
