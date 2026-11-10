/*
 * ClassicBoy - tracked allocator with canaries.
 *
 * The tracker is intentionally always enabled: the core performs only a
 * handful of allocations (ROM, cartridge RAM, save path), so the cost is
 * irrelevant and the diagnostics are invaluable for the stability tests.
 */
#include "classicboy/common.h"

#include <stdlib.h>

#define GB_ALLOC_MAGIC 0x47424C4Bu /* 'GBLK' */
#define GB_END_CANARY 0xDEADBEEFCAFEBABEull
#define GB_FREE_POISON 0xDDu

typedef struct GbAllocBlock {
    size_t size;
    uint32_t magic;
    uint32_t reserved;
    struct GbAllocBlock *prev;
    struct GbAllocBlock *next;
} GbAllocBlock;

typedef union GbAllocHeader {
    GbAllocBlock block;
    max_align_t align;
} GbAllocHeader;

static GbAllocBlock *g_blocks;
static size_t g_live_blocks;
static size_t g_live_bytes;
static size_t g_peak_bytes;
static size_t g_corrupt_blocks;

static uint8_t *block_data(GbAllocBlock *block)
{
    return (uint8_t *)((GbAllocHeader *)block + 1);
}

static GbAllocBlock *data_block(void *ptr)
{
    return &((GbAllocHeader *)ptr)[-1].block;
}

static void write_end_canary(GbAllocBlock *block)
{
    uint64_t canary = GB_END_CANARY;
    memcpy(block_data(block) + block->size, &canary, sizeof(canary));
}

static bool check_block(GbAllocBlock *block)
{
    bool ok = true;
    if (block->magic != GB_ALLOC_MAGIC) {
        ok = false;
    }
    uint64_t canary = 0;
    memcpy(&canary, block_data(block) + block->size, sizeof(canary));
    if (canary != GB_END_CANARY) {
        ok = false;
    }
    return ok;
}

static void report_corruption(GbAllocBlock *block, const char *what)
{
    g_corrupt_blocks++;
    fprintf(stderr,
            "[classicboy] memory corruption detected (%s): block=%p size=%zu\n",
            what, (void *)block_data(block), block->size);
}

void *gb_alloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }
    if (size > SIZE_MAX - sizeof(GbAllocHeader) - sizeof(uint64_t)) {
        return NULL;
    }
    GbAllocHeader *header = (GbAllocHeader *)malloc(sizeof(GbAllocHeader) + size +
                                                    sizeof(uint64_t));
    if (!header) {
        return NULL;
    }
    GbAllocBlock *block = &header->block;
    block->size = size;
    block->magic = GB_ALLOC_MAGIC;
    block->reserved = 0;
    block->prev = NULL;
    block->next = g_blocks;
    if (g_blocks) {
        g_blocks->prev = block;
    }
    g_blocks = block;
    write_end_canary(block);
    g_live_blocks++;
    g_live_bytes += size;
    if (g_live_bytes > g_peak_bytes) {
        g_peak_bytes = g_live_bytes;
    }
    return block_data(block);
}

void *gb_calloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) {
        return NULL;
    }
    size_t total = count * size;
    void *ptr = gb_alloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *gb_realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return gb_alloc(size);
    }
    if (size == 0) {
        gb_free(ptr);
        return NULL;
    }
    GbAllocBlock *block = data_block(ptr);
    if (block->magic != GB_ALLOC_MAGIC || !check_block(block)) {
        report_corruption(block, "realloc of invalid block");
        return NULL;
    }
    size_t old_size = block->size;
    void *fresh = gb_alloc(size);
    if (!fresh) {
        return NULL;
    }
    memcpy(fresh, ptr, old_size < size ? old_size : size);
    gb_free(ptr);
    return fresh;
}

void gb_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    GbAllocBlock *block = data_block(ptr);
    if (block->magic != GB_ALLOC_MAGIC) {
        fprintf(stderr, "[classicboy] gb_free on unknown pointer %p\n", ptr);
        return;
    }
    if (!check_block(block)) {
        report_corruption(block, "free");
    }
    memset(ptr, GB_FREE_POISON, block->size);
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        g_blocks = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->magic = 0;
    g_live_blocks--;
    g_live_bytes -= block->size;
    free(block);
}

char *gb_strdup(const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = (char *)gb_alloc(len + 1);
    if (copy) {
        memcpy(copy, text, len + 1);
    }
    return copy;
}

size_t gb_alloc_check(void)
{
    size_t bad = 0;
    for (GbAllocBlock *block = g_blocks; block; block = block->next) {
        if (!check_block(block)) {
            report_corruption(block, "canary scan");
            bad++;
        }
    }
    return bad + g_corrupt_blocks;
}

void gb_alloc_stats(size_t *live_blocks, size_t *live_bytes,
                    size_t *peak_bytes)
{
    if (live_blocks) {
        *live_blocks = g_live_blocks;
    }
    if (live_bytes) {
        *live_bytes = g_live_bytes;
    }
    if (peak_bytes) {
        *peak_bytes = g_peak_bytes;
    }
}

void gb_alloc_report(FILE *out)
{
    if (!out) {
        out = stderr;
    }
    fprintf(out,
            "[classicboy] allocator: %zu live block(s), %zu live byte(s), "
            "peak %zu byte(s), %zu corruption report(s)\n",
            g_live_blocks, g_live_bytes, g_peak_bytes, g_corrupt_blocks);
}
