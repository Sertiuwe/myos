#include "memory.h"

#define HEAP_SIZE (8 * 1024 * 1024)

typedef struct block_header {
    uint64_t size;
    uint8_t is_free;
    struct block_header* next;
    struct block_header* prev;
} block_header_t;

__attribute__((aligned(16)))
static uint8_t heap_memory[HEAP_SIZE];

static block_header_t* head = NULL;

void kinit_heap(void) {
    head = (block_header_t*)heap_memory;
    head->size = HEAP_SIZE - sizeof(block_header_t);
    head->is_free = 1;
    head->next = NULL;
    head->prev = NULL;
}

void* kmalloc(uint64_t size) {
    if (size == 0) return NULL;
    size = (size + 15) & ~15;

    block_header_t* current = head;
    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            if (current->size >= size + sizeof(block_header_t) + 16) {
                block_header_t* new_block = (block_header_t*)((uint8_t*)current + sizeof(block_header_t) + size);
                new_block->size = current->size - size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->next = current->next;
                new_block->prev = current;
                if (current->next != NULL) current->next->prev = new_block;
                current->next = new_block;
                current->size = size;
            }
            current->is_free = 0;
            return (void*)((uint8_t*)current + sizeof(block_header_t));
        }
        current = current->next;
    }
    return NULL;
}

void kfree(void* ptr) {
    if (ptr == NULL) return;
    block_header_t* header = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    header->is_free = 1;

    if (header->prev != NULL && header->prev->is_free) {
        header->prev->size += sizeof(block_header_t) + header->size;
        header->prev->next = header->next;
        if (header->next != NULL) header->next->prev = header->prev;
        header = header->prev;
    }
    if (header->next != NULL && header->next->is_free) {
        header->size += sizeof(block_header_t) + header->next->size;
        header->next = header->next->next;
        if (header->next != NULL) header->next->prev = header;
    }
}
