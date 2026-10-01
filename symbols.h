#ifndef SYMBOLS_H
#define SYMBOLS_H

#include "types.h"

typedef struct {
    const char *name;
    uint32_t    addr;
    uint32_t    size;
} sym_entry_t;

extern const sym_entry_t g_symbols[];
extern const int g_symbol_count;

/* Найти символ по имени, вернуть адрес или 0 */
uint32_t sym_lookup(const char *name);

/* Найти символ по адресу (для addr2sym) */
const char *sym_by_addr(uint32_t addr, uint32_t *offset);

/* Дамп всех символов (для команды symbols) */
void sym_dump_all(void);

/* Дамп символов в диапазоне адресов */
void sym_dump_range(uint32_t start, uint32_t end);

#endif
