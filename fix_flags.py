import pathlib, re

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

# Найти все строки вида:  X = arg2[0] ? (std::atoi(arg2)!=0) : !X;
pattern = re.compile(
    r'(\w+(?:\.\w+)?)\s*=\s*arg2\[0\]\s*\?\s*\(\s*std::atoi\(arg2\)\s*!=\s*0\s*\)\s*:\s*!\s*(\w+(?:\.\w+)?)\s*;'
)

def repl(m):
    lhs = m.group(1)
    print(f"[+] исправляю: {lhs}")
    return (f'{{ const char* _a = arg2; '
            f'if (_a[0]==\'o\'&&_a[1]==\'n\') {lhs} = true; '
            f'else if (_a[0]==\'o\'&&_a[1]==\'f\') {lhs} = false; '
            f'else if (_a[0]>=\'0\'&&_a[0]<=\'9\') {lhs} = (std::atoi(_a)!=0); '
            f'else {lhs} = !{lhs}; }}')

src, n = pattern.subn(repl, src)
print(f"Заменено вхождений: {n}")

p.write_text(src)
print("готово")
