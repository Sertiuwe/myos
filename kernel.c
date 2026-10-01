/* ============================================================
 * MyOS v0.3 — с модулями print.c и memmap.c
 * ============================================================ */
#include "types.h"
#include "print.h"
#include "memmap.h"
#include "match.h"
#include "panic.h"
#include "selftest.h"
#include "fs.h"
#include "sdcard.h"
#include "symbols.h"
#include "eth.h"
#include "net.h"
#include "lcd.h"
#include "video.h"

extern char _end;

#define UART0_BASE   0x10009000u
#define TIMER0_BASE  0x101E2000u
#define VIC_BASE     0x10140000u

#define UART_DR      (*(volatile uint32_t*)(UART0_BASE + 0x00))
#define UART_FR      (*(volatile uint32_t*)(UART0_BASE + 0x18))
#define UART_IBRD    (*(volatile uint32_t*)(UART0_BASE + 0x24))
#define UART_FBRD    (*(volatile uint32_t*)(UART0_BASE + 0x28))
#define UART_LCR_H   (*(volatile uint32_t*)(UART0_BASE + 0x2C))
#define UART_CR      (*(volatile uint32_t*)(UART0_BASE + 0x30))
#define UART_IMSC    (*(volatile uint32_t*)(UART0_BASE + 0x38))
#define UART_ICR     (*(volatile uint32_t*)(UART0_BASE + 0x44))

#define TIMER0_LOAD    (*(volatile uint32_t*)(TIMER0_BASE + 0x00))
#define TIMER0_CONTROL (*(volatile uint32_t*)(TIMER0_BASE + 0x08))
#define TIMER0_INTCLR  (*(volatile uint32_t*)(TIMER0_BASE + 0x0C))

#define VIC_IRQSTATUS  (*(volatile uint32_t*)(VIC_BASE + 0x00))
#define VIC_INTENABLE  (*(volatile uint32_t*)(VIC_BASE + 0x10))
#define VIC_VECTADDR   (*(volatile uint32_t*)(VIC_BASE + 0x30))
#define VIC_DEFVECT    (*(volatile uint32_t*)(VIC_BASE + 0x34))
#define VIC_VECTADDR0  (*(volatile uint32_t*)(VIC_BASE + 0x100))
#define VIC_VECTCNTL0  (*(volatile uint32_t*)(VIC_BASE + 0x200))
#define VIC_VECTADDR1  (*(volatile uint32_t*)(VIC_BASE + 0x104))
#define VIC_VECTCNTL1  (*(volatile uint32_t*)(VIC_BASE + 0x204))

#define MAX_TASKS    4
#define STACK_SIZE   2048
#define RX_BUF_SIZE  256
#define RX_BUF_MASK  255
#define CMD_HISTORY  8

typedef struct {
    uint32_t *sp;
    int       alive;
    char      name[8];
    uint32_t  stack[STACK_SIZE];
} task_t;

task_t tasks[MAX_TASKS];
int    current = -1;
int    ntasks  = 0;
volatile uint32_t tick_count = 0;
volatile uint32_t idle_count = 0;
volatile uint32_t counter_value = 0;

const unsigned int task_t_size = sizeof(task_t);
static uint8_t g_cwd = FS_ROOT;

static volatile char rx_buf[RX_BUF_SIZE];
static volatile int  rx_head = 0;
static volatile int  rx_tail = 0;

static char g_history[CMD_HISTORY][64];
static int  history_count = 0;
static int  history_next = 0;

static void rx_push(char c) {
    int next = (rx_head + 1) & RX_BUF_MASK;
    if (next != rx_tail) {
        rx_buf[rx_head] = c;
        rx_head = next;
    }
}
static int rx_pop(char *c) {
    if (rx_head == rx_tail) return 0;
    *c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & RX_BUF_MASK;
    return 1;
}

static void uart_init(void) {
    UART_CR = 0;
    UART_IBRD = 13;
    UART_FBRD = 1;
    UART_LCR_H = 0x70;
    UART_CR = 0x301;
    UART_IMSC = 0x10;
}

static void timer_init(void) {
    TIMER0_LOAD    = 10000;
    TIMER0_CONTROL = 0xE2;
}

extern void irq_handler(void);

static void vic_init(void) {
    VIC_VECTADDR0 = (uint32_t)irq_handler;
    VIC_VECTCNTL0 = 0x20 | 4;
    VIC_VECTADDR1 = (uint32_t)irq_handler;
    VIC_VECTCNTL1 = 0x20 | 12;
    VIC_DEFVECT   = (uint32_t)irq_handler;
    VIC_INTENABLE = (1u << 4) | (1u << 12) | (1u << 15);
}

void irq_dispatch(void) {
    uint32_t status = VIC_IRQSTATUS;
    if (status & (1u << 4)) {
        TIMER0_INTCLR = 1;
        tick_count++;
    }
    if (status & (1u << 12)) {
        uint32_t c = UART_DR;
        rx_push((char)(c & 0xFF));
        UART_ICR = 0x10;
    }
    if (status & (1u << 15)) {
        eth_irq_handler();
    }
    VIC_VECTADDR = 0;
}

void schedule(void) {
    int start = current;
    do {
        current++;
        if (current >= ntasks) current = 0;
        if (tasks[current].alive) return;
    } while (current != start);
}

#define yield() __asm__ volatile ("swi 0")

/* ============================================================
 * Утилиты
 * ============================================================ */
static int streq(const char *a, const char *b) {
    const volatile char *va = a;
    const volatile char *vb = b;
    for (int i = 0; i < 32; i++) {
        char ca = va[i], cb = vb[i];
        if (ca != cb) return 0;
        if (ca == 0) return 1;
    }
    return 0;
}

static void scopy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int parse_hex(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int any = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | d;
        any = 1;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

static int parse_dec(const char *s, uint32_t *out);

static int parse_ip(const char *s, uint8_t out[4]) {
    char tmp[20];
    int i = 0;
    while (s[i] && i < 19) { tmp[i] = s[i]; i++; }
    tmp[i] = 0;

    int n = 0;
    char *p = tmp;
    while (n < 4) {
        char buf[4];
        int bi = 0;
        while (*p >= '0' && *p <= '9' && bi < 3) buf[bi++] = *p++;
        buf[bi] = 0;
        if (bi == 0) return 0;
        uint32_t v = 0;
        if (!parse_dec(buf, &v)) return 0;
        if (v > 255) return 0;
        out[n++] = (uint8_t)v;
        if (*p == 0) break;
        if (*p != '.') return 0;
        p++;
    }
    return (n == 4 && *p == 0);
}

static int parse_dec(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        any = 1;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

static void history_add(const char *line) {
    if (!line[0]) return;
    scopy(g_history[history_next], line, 64);
    history_next = (history_next + 1) % CMD_HISTORY;
    if (history_count < CMD_HISTORY) history_count++;
}

/* ============================================================
 * Команды
 * ============================================================ */
static void cmd_help(void) {
    puts("MyOS v0.3 commands:\n");
    puts("  help              this help\n");
    puts("  echo <s>          print string\n");
    puts("  ps                list tasks\n");
    puts("  tick              current tick\n");
    puts("  uptime            system uptime\n");
    puts("  tasks             task counters\n");
    puts("  cpu               CPU info\n");
    puts("  mem               memory info\n");
    puts("  memmap            memory regions\n");
    puts("  heap              heap status\n");
    puts("  peek <addr>       read word\n");
    puts("  poke <addr> <val> write word\n");
    puts("  hexdump <a> <n>   dump n bytes\n");
    puts("  history           last commands\n");
    puts("  symbols [range]   list symbols\n");
    puts("  sym <name>        find symbol by name\n");
    puts("  ver               version\n");
    puts("  clear             clear screen\n");
    puts("  reboot            halt system\n");
    puts("  ifconfig          show eth0 config\n");
    puts("  arp               show ARP table\n");
    puts("  netstat           network stats\n");
    puts("  ping <ip> [n]     ICMP echo\n");
    puts("  nslookup <host>   DNS resolve\n");
    puts("  netpoll           poll eth RX\n");
}

static void cmd_ps(void) {
    puts("PID NAME    STATE\n");
    for (int i = 0; i < ntasks; i++) {
        puts("  ");
        putuint((uint32_t)i);
        puts("   ");
        puts(tasks[i].name);
        int n = 0;
        while (tasks[i].name[n]) n++;
        for (int p = n; p < 8; p++) putc(' ');
        puts(tasks[i].alive ? "ready" : "dead");
        puts("\n");
    }
}

static void cmd_tasks(void) {
    puts("idle_count    = "); putuint(idle_count); puts("\n");
    puts("counter_value = "); putuint(counter_value); puts("\n");
}

static void cmd_cpu(void) {
    puts("CPU: ARM7TDMI (ARMv4T)\n");
    puts("  Registers: 16 x 32-bit\n");
    puts("  Modes: User, FIQ, IRQ, SVC, Abort, Undef, System\n");
    puts("  MMU: none\n");
    puts("  Pipeline: 3-stage\n");
}

static void cmd_mem(void) {
    puts("RAM: 16 MB total\n");
    puts("  Base: 0x00000000\n");
    puts("  End:  0x01000000\n");
    puts("  Kernel ends at: 0x");
    puthex((uint32_t)&_end);
    puts("\n");
}

static void cmd_heap(void) {
    puts("Heap:\n");
    puts("  base  = 0x"); puthex((uint32_t)heap_start()); puts("\n");
    puts("  limit = 0x"); puthex((uint32_t)heap_end());   puts("\n");
    puts("  used  = ");   putuint(heap_used());           puts(" bytes\n");
    puts("  free  = ");   putuint(heap_free());           puts(" bytes\n");
    puts("  total = ");   putuint(heap_size());           puts(" bytes\n");
}

static void cmd_peek(uint32_t addr) {
    volatile uint32_t *p = (volatile uint32_t*)addr;
    puts("[0x"); puthex(addr); puts("] = 0x"); puthex(*p); puts("\n");
}

static void cmd_poke(uint32_t addr, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t*)addr;
    *p = val;
    puts("[0x"); puthex(addr); puts("] <- 0x"); puthex(val); puts("\n");
}

static void cmd_hexdump(uint32_t addr, uint32_t len) {
    if (len > 512) len = 512;
    uint32_t i = 0;
    while (i < len) {
        puthex(addr + i);
        puts(": ");
        uint32_t line = len - i;
        if (line > 16) line = 16;
        for (uint32_t j = 0; j < 16; j++) {
            if (j < line) {
                volatile uint8_t *p = (volatile uint8_t*)(addr + i + j);
                puthex8(*p);
                putc(' ');
            } else {
                puts("   ");
            }
        }
        puts(" |");
        for (uint32_t j = 0; j < line; j++) {
            volatile uint8_t *p = (volatile uint8_t*)(addr + i + j);
            char c = (char)*p;
            putc((c >= 32 && c < 127) ? c : '.');
        }
        puts("|\n");
        i += line;
    }
}

static void cmd_history(void) {
    if (history_count == 0) {
        puts("no history\n");
        return;
    }
    int start = (history_count < CMD_HISTORY) ? 0 : history_next;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % CMD_HISTORY;
        puts("  ");
        putuint((uint32_t)(i + 1));
        puts(": ");
        puts(g_history[idx]);
        puts("\n");
    }
}

static void cmd_ver(void) {
    puts("MyOS v0.3\n");
    puts("Build: " __DATE__ " " __TIME__ "\n");
    puts("Kernel ends at 0x");
    puthex((uint32_t)&_end);
    puts("\n");
}

/* ============================================================
 * Обработчик команд
 * ============================================================ */
static void execute_command(char *line) {
    while (*line == ' ') line++;
    if (line[0] == 0) return;

    history_add(line);

    char *cmd = line;
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) {
        *arg = 0;
        arg++;
        while (*arg == ' ') arg++;
    }

    char *arg2 = arg;
    while (*arg2 && *arg2 != ' ') arg2++;
    if (*arg2) {
        *arg2 = 0;
        arg2++;
        while (*arg2 == ' ') arg2++;
    }

    if      (streq(cmd, "help"))    cmd_help();
    else if (streq(cmd, "echo"))    { puts(arg); puts("\n"); }
    else if (streq(cmd, "ps"))      cmd_ps();
    else if (streq(cmd, "tick"))    { puts("tick = "); putuint(tick_count); puts("\n"); }
    else if (streq(cmd, "uptime"))  { puts("uptime: "); putuint(tick_count / 100); puts(" sec\n"); }
    else if (streq(cmd, "tasks"))   cmd_tasks();
    else if (streq(cmd, "cpu"))     cmd_cpu();
    else if (streq(cmd, "mem"))     cmd_mem();
    else if (streq(cmd, "memmap"))  memmap_dump();
    else if (streq(cmd, "heap"))    cmd_heap();
    else if (streq(cmd, "history")) cmd_history();
    else if (streq(cmd, "ver"))     cmd_ver();
    else if (streq(cmd, "clear"))   puts("\x1b[2J\x1b[H");
    else if (streq(cmd, "reboot"))  { puts("Halting...\n"); while (1) { } }
    else if (streq(cmd, "crash"))   { panic_regs("manual crash test"); }
    else if (streq(cmd, "peek")) {
        uint32_t a;
        if (parse_hex(arg, &a)) cmd_peek(a);
        else puts("usage: peek <hexaddr>\n");
    }
    else if (streq(cmd, "poke")) {
        uint32_t a, v;
        if (parse_hex(arg, &a) && parse_hex(arg2, &v)) cmd_poke(a, v);
        else puts("usage: poke <hexaddr> <hexval>\n");
    }
    else if (streq(cmd, "hexdump")) {
        uint32_t a, n;
        if (parse_hex(arg, &a) && parse_dec(arg2, &n)) cmd_hexdump(a, n);
        else puts("usage: hexdump <hexaddr> <decLen>\n");
    }
    else if (streq(cmd, "sym")) {
        puts("sym disabled\n");
    }
    else if (streq(cmd, "symbols")) {
        puts("symbols disabled\n");
    }
    else if (streq(cmd, "pwd")) {
        char buf[FS_MAX_PATH];
        fs_path(g_cwd, buf, FS_MAX_PATH);
        puts(buf);
        puts("\n");
    }
    else if (streq(cmd, "ls")) {
        int dir = g_cwd;
        if (arg[0]) {
            int r = fs_resolve_rel(arg, g_cwd);
            if (r < 0) { puts("ls: not found: "); puts(arg); puts("\n"); }
            else if (fs_type(r) != FT_DIR) { puts("ls: not a directory\n"); }
            else dir = r;
        }
        if (dir >= 0 && fs_type(dir) == FT_DIR) {
            int items[32];
            int n = fs_list((uint8_t)dir, items, 32);
            for (int i = 0; i < n; i++) {
                int e = items[i];
                if (fs_type(e) == FT_DIR) {
                    puts("  [dir]  ");
                    puts(fs_name(e));
                } else {
                    puts("  [file] ");
                    puts(fs_name(e));
                    puts("  ");
                    putuint(fs_size(e));
                    puts(" bytes");
                }
                puts("\n");
            }
            if (n == 0) puts("  (empty)\n");
        }
    }
    else if (streq(cmd, "cd")) {
        if (!arg[0]) { g_cwd = FS_ROOT; }
        else {
            int r = fs_resolve_rel(arg, g_cwd);
            if (r < 0) { puts("cd: not found: "); puts(arg); puts("\n"); }
            else if (fs_type(r) != FT_DIR) puts("cd: not a directory\n");
            else g_cwd = (uint8_t)r;
        }
    }
    else if (streq(cmd, "mkdir")) {
        if (!arg[0]) { puts("usage: mkdir <name>\n"); }
        else {
            int r = fs_mkdir(g_cwd, arg);
            if (r < 0) { puts("mkdir: failed (exists or no space)\n"); }
            else puts("ok\n");
        }
    }
    else if (streq(cmd, "touch")) {
        if (!arg[0]) { puts("usage: touch <name>\n"); }
        else {
            int r = fs_create(g_cwd, arg);
            if (r < 0) { puts("touch: failed\n"); }
            else puts("ok\n");
        }
    }
    else if (streq(cmd, "cat")) {
        if (!arg[0]) { puts("usage: cat <file>\n"); }
        else {
            int r = fs_resolve_rel(arg, g_cwd);
            if (r < 0) { puts("cat: not found\n"); }
            else if (fs_type(r) != FT_FILE) { puts("cat: not a file\n"); }
            else {
                uint32_t sz = fs_size(r);
                uint8_t buf[64];
                uint32_t off = 0;
                while (off < sz) {
                    uint32_t chunk = sz - off;
                    if (chunk > sizeof(buf)) chunk = sizeof(buf);
                    int got = fs_read(r, buf, off, chunk);
                    if (got <= 0) break;
                    for (int i = 0; i < got; i++) putc((char)buf[i]);
                    off += (uint32_t)got;
                }
                puts("\n");
            }
        }
    }
    else if (streq(cmd, "write")) {
        /* write <file> <text> */
        if (!arg[0] || !arg2[0]) { puts("usage: write <file> <text>\n"); }
        else {
            int r = fs_resolve_rel(arg, g_cwd);
            if (r < 0) {
                r = fs_create(g_cwd, arg);
                if (r < 0) { puts("write: cannot create\n"); }
            }
            if (r >= 0) {
                int tl = m_strlen(arg2);
                if (fs_write(r, (const uint8_t*)arg2, (uint32_t)tl) >= 0) {
                    puts("ok (");
                    putuint((uint32_t)tl);
                    puts(" bytes)\n");
                } else {
                    puts("write: no space\n");
                }
            }
        }
    }
    else if (streq(cmd, "rm")) {
        if (!arg[0]) { puts("usage: rm <name>\n"); }
        else {
            int r = fs_resolve_rel(arg, g_cwd);
            if (r < 0) puts("rm: not found\n");
            else if (fs_delete(r) == 0) puts("ok\n");
            else puts("rm: failed (dir not empty?)\n");
        }
    }
    else if (streq(cmd, "ifconfig")) {
        uint8_t mac[6], ip[4], gw[4];
        eth_get_mac(mac);
        net_get_myip(ip);
        net_get_gwip(gw);
        puts("eth0:\n");
        puts("  MAC "); for (int i=0;i<6;i++){puthex8(mac[i]); if(i<5)putc(':');}
        puts("\n  IP  "); net_print_ip(ip);
        puts("\n  GW  "); net_print_ip(gw);
        puts("\n");
    }
    else if (streq(cmd, "arp")) { net_show_arp(); }
    else if (streq(cmd, "netstat")) {
        uint32_t t, r, te, re;
        eth_get_stats(&t, &r, &te, &re);
        puts("eth0 stats:\n");
        puts("  TX ok="); putuint(t); puts("  err="); putuint(te); puts("\n");
        puts("  RX ok="); putuint(r); puts("  err="); putuint(re); puts("\n");
        puts("  queue="); putuint(ETH_RX_COUNT); puts("\n");
    }
    else if (streq(cmd, "ping")) {
        uint8_t ip[4];
        uint32_t n = 3;
        if (!arg[0]) { puts("usage: ping <a.b.c.d> [count]\n"); }
        else {
            uint32_t l = 0; while (arg[l]) l++;
            puts("[DBG] arg len="); putuint(l); puts(": '");
            for (uint32_t i = 0; i < l; i++) {
                uint8_t ch = (uint8_t)arg[i];
                if (ch >= 32 && ch < 127) putc((char)ch);
                else { putc('<'); puthex8(ch); putc('>'); }
            }
            puts("'\n");
            if (!parse_ip(arg, ip)) puts("bad ip\n");
            else {
                if (arg2[0]) parse_dec(arg2, &n);
                net_ping(ip, (int)n);
            }
        }
    }
    else if (streq(cmd, "nslookup") || streq(cmd, "dns")) {
        if (!arg[0]) { puts("usage: nslookup <host>\n"); }
        else {
            uint8_t ip[4];
            if (net_dns_resolve(arg, ip) == 0) {
                puts("  "); puts(arg); puts(" -> ");
                net_print_ip(ip); puts("\n");
            }
        }
    }
    else if (streq(cmd, "netpoll")) {
        net_poll();
        puts("polled\n");
    }
    else if (streq(cmd, "sync")) {
        if (!sd_present()) { puts("no SD card\n"); }
        else if (fs_sync_to_disk() == 0) puts("synced to SD\n");
        else puts("sync FAILED\n");
    }
    else if (streq(cmd, "sd")) {
        puts("SD card status:\n");
        if (sd_present()) {
            puts("  present = yes\n");
            puts("  size    = ");
            putuint(sd_total_blocks());
            puts(" blocks (");
            putuint(sd_total_blocks() / 2);
            puts(" KB)\n");
        } else {
            puts("  present = no\n");
        }
    }
    else if (streq(cmd, "df")) {
        puts("MyFS:\n");
        puts("  total blocks = "); putuint(fs_total_blocks()); puts("\n");
        puts("  free  blocks = "); putuint(fs_free_blocks());  puts("\n");
        puts("  total entries= "); putuint(fs_total_entries()); puts("\n");
        puts("  free  entries= "); putuint(fs_free_entries());  puts("\n");
        puts("  block size   = "); putuint(FS_BLOCK_SIZE);     puts(" bytes\n");
    }
    else if (streq(cmd, "format")) {
        fs_format();
        g_cwd = FS_ROOT;
        puts("filesystem formatted\n");
    }
    else {
        puts("Unknown command: ");
        puts(cmd);
        puts("\nTry 'help'\n");
    }
}

/* ============================================================
 * Задачи
 * ============================================================ */
static void task_terminal(void) {
    char line[80];
    int  len = 0;

    puts("\nType 'help' for commands.\n\n");
    puts("myos> ");

    while (1) {
        char c;
        if (rx_pop(&c)) {
            if (c == '\r' || c == '\n') {
                puts("\n");
                line[len] = 0;
                execute_command(line);
                len = 0;
                puts("myos> ");
            } else if (c == 8 || c == 127) {
                if (len > 0) { len--; puts("\b \b"); }
            } else if (c >= 32 && c < 127) {
                if (len < 79) { line[len++] = c; putc(c); }
            }
        } else {
            yield();
        }
    }
}

static void task_idle(void) {
    while (1) { idle_count++; yield(); }
}

static void task_counter(void) {
    while (1) { counter_value++; yield(); }
}

static void task_create(int idx, void (*entry)(void), const char *name) {
    task_t *t = &tasks[idx];

    uint32_t *sp = &t->stack[STACK_SIZE];
    sp = (uint32_t*)((uint32_t)sp & ~7u);

    *(--sp) = (uint32_t)entry;
    for (int i = 0; i < 13; i++) *(--sp) = 0;
    *(--sp) = 0x13;

    t->sp = sp;
    t->alive = 1;
    int i = 0;
    while (name[i] && i < 7) { t->name[i] = name[i]; i++; }
    t->name[i] = 0;
}

void kernel_main(void) {
    uart_init();

    puts("\n");
    puts("+--------------------------------+\n");
    puts("|      MyOS v0.3                 |\n");
    puts("|      ARM7TDMI kernel           |\n");
    puts("+--------------------------------+\n\n");

    puts("Init memmap...\n");
    memmap_init();
    puts("  kernel ends at 0x");
    puthex((uint32_t)&_end);
    puts("\n  heap: ");
    putuint(heap_size());
    puts(" bytes\n");

    puts("Init VIC...\n");
    vic_init();

    puts("Init timer (100 Hz)...\n");
    timer_init();

    puts("Init eth0...\n");
    {
        static const uint8_t mac[6] = {0x02,0x00,0x00,0x00,0x00,0x42};
        eth_init(mac);
    }
    puts("  eth0 ready\n");

    puts("Initializing SD card...\n");
    if (sd_present()) {
        puts("  card present, blocks = ");
        putuint(sd_total_blocks());
        puts("\n");
    } else {
        puts("  no card (RAM-only mode)\n");
    }

    puts("Init net...\n");
    net_init();
    puts("  net ready\n");

    puts("Initializing MyFS...\n");
    fs_init();
    puts("  filesystem ready\n");

    puts("Init LCD (320x240 RGB565)...\n");
    lcd_init(160, 120, 0x00200000);

    lcd_clear(LCD_BLACK);
    {
        int bar_h = 120 / 6;
        lcd_fill_rect(  0, 0*bar_h, 160, bar_h, LCD_RED);
        lcd_fill_rect(  0, 1*bar_h, 160, bar_h, LCD_YELLOW);
        lcd_fill_rect(  0, 2*bar_h, 160, bar_h, LCD_GREEN);
        lcd_fill_rect(  0, 3*bar_h, 160, bar_h, LCD_CYAN);
        lcd_fill_rect(  0, 4*bar_h, 160, bar_h, LCD_BLUE);
        lcd_fill_rect(  0, 5*bar_h, 160, bar_h, LCD_MAGENTA);
    }
    lcd_enable();
    puts("  LCD ready\n");

    puts("Init video player...\n");
    {
        video_t vid;
        int r = video_load(2000, &vid);
        if (r == 0) {
            puts("  MV01: ");
            putuint(vid.width); puts("x"); putuint(vid.height);
            puts(", "); putuint(vid.frames); puts(" frames\n");
            lcd_clear(LCD_BLACK);
            video_play(&vid);
            puts("  video done\n");
        } else {
            puts("  no video on SD (err="); putuint(-r); puts(")\n");
        }
    }

    puts("Creating tasks...\n");
    task_create(0, task_terminal, "term");
    task_create(1, task_idle,     "idle");
    task_create(2, task_counter,  "cnt");
    ntasks = 3;

    /* Self-test перед запуском */
    int fails = selftest_run();
    if (fails > 0) {
        panic_regs("Boot self-test FAILED");
    }

    puts("Starting scheduler...\n");

    __asm__ volatile (
        "mrs r0, cpsr        \n"
        "bic r0, r0, #0xC0   \n"
        "msr cpsr_c, r0      \n"
        ::: "r0"
    );

    yield();

    while (1) { }
}
