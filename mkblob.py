import sys
src, dst, sym = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, 'rb').read()
with open(dst, 'w') as f:
    f.write("/* generated from %s */\n" % src)
    f.write("const unsigned char %s[] = {\n" % sym)
    for i in range(0, len(data), 16):
        f.write("  " + ",".join(str(b) for b in data[i:i+16]) + ",\n")
    f.write("};\nconst unsigned int %s_len = %d;\n" % (sym, len(data)))
print("blob: %s -> %s (%d bytes)" % (src, dst, len(data)))
