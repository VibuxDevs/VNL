/*
 * VNL Kernel Heap — boundary-tag (free-list) allocator
 *
 * Layout of an allocated block:
 *   [BlockHeader][...user data...][BlockFooter]
 *
 * Free blocks additionally have prev/next pointers inside their data area.
 */
#include "heap.h"
#include "string.h"
#include "panic.h"
#include "vmm.h"
#include "pmm.h"

#define BLOCK_FREE  0x0
#define BLOCK_USED  0x1
#define MIN_BLOCK   (sizeof(BlockHeader) + sizeof(BlockFooter) + 16)
#define ALIGN8(n)   ALIGN_UP(n, 8)

typedef struct BlockHeader {
    size_t size;       /* includes header + footer, bit0 = alloc flag */
    struct BlockHeader *prev_free;
    struct BlockHeader *next_free;
} BlockHeader;

typedef struct {
    size_t size;       /* mirrors header size field */
} BlockFooter;

static BlockHeader *free_list = NULL;
static uintptr_t heap_end;

/* ---- Internal helpers -------------------------------------------- */

static inline size_t blk_size(BlockHeader *h) { return h->size & ~1ULL; }
static inline bool   blk_used(BlockHeader *h) { return h->size & 1;     }

static inline BlockFooter *get_footer(BlockHeader *h)
{
    return (BlockFooter *)((uint8_t *)h + blk_size(h) - sizeof(BlockFooter));
}

static inline BlockHeader *next_block(BlockHeader *h)
{
    return (BlockHeader *)((uint8_t *)h + blk_size(h));
}

static inline BlockHeader *prev_block(BlockHeader *h)
{
    BlockFooter *f = (BlockFooter *)((uint8_t *)h - sizeof(BlockFooter));
    return (BlockHeader *)((uint8_t *)h - (f->size & ~1ULL));
}

static void set_size(BlockHeader *h, size_t sz, bool used)
{
    h->size = sz | (size_t)used;
    get_footer(h)->size = sz | (size_t)used;
}

static void free_list_remove(BlockHeader *h)
{
    if (h->prev_free) h->prev_free->next_free = h->next_free;
    else              free_list = h->next_free;
    if (h->next_free) h->next_free->prev_free = h->prev_free;
    h->prev_free = h->next_free = NULL;
}

static void free_list_insert(BlockHeader *h)
{
    h->next_free = free_list;
    h->prev_free = NULL;
    if (free_list) free_list->prev_free = h;
    free_list = h;
}

/* Expand the heap by mapping more physical pages */
static void heap_expand(size_t extra)
{
    extra = ALIGN_UP(extra, PAGE_SIZE);
    for (size_t off = 0; off < extra; off += PAGE_SIZE) {
        void *frame = pmm_alloc();
        if (!frame) kpanic("heap: OOM expanding heap");
        vmm_map(heap_end + off, (uint64_t)frame,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE);
    }

    /* Create a free block covering the new region */
    BlockHeader *h = (BlockHeader *)heap_end;
    set_size(h, extra, false);
    h->prev_free = h->next_free = NULL;
    heap_end += extra;

    /* Try coalescing with block before */
    /* (For simplicity, just insert as a free block) */
    free_list_insert(h);
}

/* ---- Public API -------------------------------------------------- */

void heap_init(uintptr_t base, size_t initial_size)
{
    initial_size = ALIGN_UP(initial_size, PAGE_SIZE);
    heap_end = base;

    /* Map initial pages */
    for (size_t off = 0; off < initial_size; off += PAGE_SIZE) {
        void *frame = pmm_alloc();
        if (!frame) kpanic("heap_init: OOM");
        vmm_map(base + off, (uint64_t)frame,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITE);
    }

    /* Single free block */
    BlockHeader *h = (BlockHeader *)base;
    set_size(h, initial_size, false);
    h->prev_free = h->next_free = NULL;
    free_list = h;
    heap_end = base + initial_size;
}

void *kmalloc(size_t size)
{
    if (size == 0) return NULL;
    size = ALIGN8(size + sizeof(BlockHeader) + sizeof(BlockFooter));
    if (size < MIN_BLOCK) size = MIN_BLOCK;

    /* First-fit search */
    BlockHeader *h = free_list;
    while (h) {
        if (blk_size(h) >= size) {
            free_list_remove(h);
            /* Split if leftover is large enough */
            size_t leftover = blk_size(h) - size;
            if (leftover >= MIN_BLOCK) {
                set_size(h, size, true);
                BlockHeader *rest = next_block(h);
                set_size(rest, leftover, false);
                rest->prev_free = rest->next_free = NULL;
                free_list_insert(rest);
            } else {
                set_size(h, blk_size(h), true);
            }
            return (void *)((uint8_t *)h + sizeof(BlockHeader));
        }
        h = h->next_free;
    }

    /* No fit — expand heap */
    heap_expand(size + PAGE_SIZE);
    return kmalloc(size);   /* retry */
}

void *kcalloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr) return kmalloc(size);
    BlockHeader *h = (BlockHeader *)((uint8_t *)ptr - sizeof(BlockHeader));
    size_t old_size = blk_size(h) - sizeof(BlockHeader) - sizeof(BlockFooter);
    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_size < size ? old_size : size);
    kfree(ptr);
    return new_ptr;
}

void kfree(void *ptr)
{
    if (!ptr) return;
    BlockHeader *h = (BlockHeader *)((uint8_t *)ptr - sizeof(BlockHeader));
    ASSERT(blk_used(h));
    set_size(h, blk_size(h), false);

    /* Coalesce with next block */
    BlockHeader *nxt = next_block(h);
    if ((uintptr_t)nxt < heap_end && !blk_used(nxt)) {
        free_list_remove(nxt);
        set_size(h, blk_size(h) + blk_size(nxt), false);
    }

    /* Coalesce with previous block (if not the first block) */
    /* We use a simple sentinel: check that prev footer is in heap */
    if ((uintptr_t)h > (heap_end - (heap_end - (uintptr_t)free_list))) {
        /* Skip coalesce-prev for simplicity to avoid sentinel complexity */
    }

    free_list_insert(h);
}
