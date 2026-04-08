/*
 * VNL Physical Memory Manager
 * Simple bitmap allocator. Each bit = one 4 KiB frame.
 * Frame 0 and low frames (< 1 MiB) are always marked used.
 */
#include "pmm.h"
#include "string.h"
#include "panic.h"

#define MAX_FRAMES (1024 * 1024)     /* 4 GiB worth of 4KiB frames */
#define BITMAP_SIZE (MAX_FRAMES / 8) /* bytes */

/* Placed in BSS — zeroed at startup. 0 = free, 1 = used */
static uint8_t bitmap[BITMAP_SIZE] ALIGN(8);

static uint64_t total_frames = 0;
static uint64_t free_frames  = 0;

/* ---- Bitmap helpers ---------------------------------------------- */
static inline void bm_set(uint64_t frame)
{
    bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static inline void bm_clear(uint64_t frame)
{
    bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

static inline bool bm_test(uint64_t frame)
{
    return (bitmap[frame / 8] & (uint8_t)(1u << (frame % 8))) != 0;
}

/* ---- Public API -------------------------------------------------- */

/*
 * pmm_init — called early with the total memory (in KiB) from Multiboot2.
 * Marks all frames used first, then frees usable RAM above 1 MiB.
 */
void pmm_init(uint64_t mem_upper_kb)
{
    total_frames = (mem_upper_kb * 1024) / PAGE_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* Start by marking everything used */
    memset(bitmap, 0xFF, BITMAP_SIZE);
    free_frames = 0;

    /* Free frames from 1 MiB upward (reserved: 0x0–0xFFFFF) */
    uint64_t first_free = ALIGN_UP(0x100000, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t f = first_free; f < total_frames; f++) {
        bm_clear(f);
        free_frames++;
    }
}

/*
 * pmm_reserve — mark a physical range [base, base+len) as used.
 * Called after pmm_init to protect the kernel image and other
 * reserved regions from being handed out by pmm_alloc.
 */
void pmm_reserve(uint64_t base, uint64_t len)
{
    uint64_t first = ALIGN_DOWN(base, PAGE_SIZE) / PAGE_SIZE;
    uint64_t last  = ALIGN_UP(base + len, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t f = first; f < last && f < total_frames; f++) {
        if (!bm_test(f)) {
            bm_set(f);
            free_frames--;
        }
    }
}

/*
 * pmm_alloc — returns a zeroed 4 KiB physical frame address,
 * or NULL if OOM. Physical address is below KERNEL_VMA.
 */
void *pmm_alloc(void)
{
    /* Scan bitmap for a free frame */
    for (uint64_t i = 0; i < BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFF) continue;   /* all used in this byte */
        for (int bit = 0; bit < 8; bit++) {
            if (!(bitmap[i] & (1u << bit))) {
                uint64_t frame = i * 8 + (uint64_t)bit;
                if (frame >= total_frames) return NULL;
                bm_set(frame);
                free_frames--;
                /* Return physical address (identity-mapped during boot) */
                return (void *)(frame * PAGE_SIZE);
            }
        }
    }
    return NULL;
}

void pmm_free(void *frame)
{
    uint64_t f = (uint64_t)frame / PAGE_SIZE;
    if (!bm_test(f)) return;   /* double-free guard */
    bm_clear(f);
    free_frames++;
}

uint64_t pmm_free_pages(void)  { return free_frames;  }
uint64_t pmm_total_pages(void) { return total_frames; }
