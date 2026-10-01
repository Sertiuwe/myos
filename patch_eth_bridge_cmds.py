import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "gw") == 0) {'''
new = '''            if (std::strcmp(arg1, "bridge") == 0) {
                if      (arg2[0]=='o' && arg2[1]=='n')  e.udp_bridge_open();
                else if (arg2[0]=='o' && arg2[1]=='f')  e.udp_bridge_close();
                else if (arg2[0]) { std::printf("bridge: on|off\\n"); return false; }
                std::printf("udp_bridge=%d  tx=%u rx=%u\\n",
                            (int)e.udp_bridge_on, e.udp_bridge_tx, e.udp_bridge_rx);
                return false;
            }
            if (std::strcmp(arg1, "gw") == 0) {'''

if old in src and '"bridge"' not in src:
    src = src.replace(old, new, 1); print("[+] eth bridge")
else:
    print("[=] или уже есть")
p.write_text(src)
