#include "idt.h"
#include "kstdio.h"
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t offset_low;   /* bits 0-15 */
    uint16_t selector;     /* code segment selector */
    uint8_t  ist;          /* bits 0-2: IST index, rest reserved */
    uint8_t  type_attr;    /* bits 0-3: type, 4: zero, 5-6: DPL, 7: present */
    uint16_t offset_mid;   /* bits 16-31 */
    uint32_t offset_high;  /* bits 32-63 */
    uint32_t reserved;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

__attribute__((aligned(16)))
static idt_entry_t idt[256];

/* ASM stubs declared in isr_stubs.asm */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);
extern void isr_stub_default(void);
extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_44(void);

static const char *exc_names[] = {
    "#DE Divide Error",           "#DB Debug",              "#NMI NMI",
    "#BP Breakpoint",             "#OF Overflow",           "#BR BOUND Range",
    "#UD Invalid Opcode",         "#NM No Math Coproc",     "#DF Double Fault",
    "Coprocessor Segment Overrun","#TS Invalid TSS",        "#NP Segment Not Present",
    "#SS Stack-Segment Fault",    "#GP General Protection", "#PF Page Fault",
    "Reserved",                   "#MF x87 FPU Error",      "#AC Alignment Check",
    "#MC Machine Check",          "#XM SIMD FP Exception",  "#VE Virtualization",
    "#CP Control Protection",     "Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
    "#HV Hypervisor Injection","#VMM Communication","#SX Security Exception","Reserved"
};

/* C-level exception handler called from ASM stubs */
void exception_handler(uint64_t vec, uint64_t err_code,
                       uint64_t rip, uint64_t rsp, uint64_t rbp,
                       uint64_t cr2_val) {
    kprintf("\n*** EXCEPTION ***\n");
    if (vec < 32)
        kprintf("Vector: %u (%s)\n", (unsigned)vec, exc_names[vec]);
    else
        kprintf("Vector: %u (IRQ/external)\n", (unsigned)vec);
    kprintf("Error code: 0x%x\n", (unsigned)err_code);
    kprintf("RIP: 0x%lx\n", rip);
    kprintf("RSP: 0x%lx\n", rsp);
    kprintf("RBP: 0x%lx\n", rbp);
    if (vec == 14)
        kprintf("CR2: 0x%lx\n", cr2_val);
    kprintf("SYSTEM HALTED\n");
    for (;;) __asm__ volatile("hlt");
}

static uint16_t get_cs(void) {
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static void set_gate(uint8_t vec, void (*stub)(void), uint8_t dpl) {
    uint64_t addr = (uint64_t)(uintptr_t)stub;
    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = get_cs();  /* используем РЕАЛЬНЫЙ CS */
    idt[vec].ist         = 0;
    idt[vec].type_attr   = 0x8E | (dpl << 5);  /* present, ring dpl, interrupt gate */
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)(addr >> 32);
    idt[vec].reserved    = 0;
}

void idt_init(void) {
    /* Default stub for all vectors first */
    for (int i = 0; i < 256; i++)
        set_gate((uint8_t)i, isr_stub_default, 0);

    /* Exception handlers 0-31 */
    static void (*stubs[])(void) = {
        isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
        isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
        isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
        isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
        isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
        isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
        isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
        isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31,
    };
    for (int i = 0; i < 32; i++)
        set_gate((uint8_t)i, stubs[i], 0);

    /* IRQ handlers */
    set_gate(32, isr_stub_32, 0);  /* IRQ0 → PIT */
    set_gate(33, isr_stub_33, 0);  /* IRQ1 → keyboard */
    set_gate(44, isr_stub_44, 0);  /* IRQ12 → PS/2 mouse */

    /* Load IDT */
    idtr_t idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)(uintptr_t)&idt;
    __asm__ volatile("lidt %0" :: "m"(idtr));

    kprintf("IDT: loaded, CS=0x%x, gate[0].type_attr=0x%x, sel=0x%x\n",
            get_cs(), idt[0].type_attr, idt[0].selector);
    kprintf("IDT: 256 entries, exceptions 0-31 registered\n");
}
