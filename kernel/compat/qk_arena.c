/* SPDX-License-Identifier: MIT */
#include "qk_arena.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#define QK_ALIGN        16u
#define QK_MIN_PAYLOAD  32u            /* Holds the two free-list pointers. */
#define QK_MAGIC_USED   0x514b5553u    /* "QKUS" */
#define QK_MAGIC_FREE   0x514b4652u    /* "QKFR" */

/* Blocks sit back to back. prev_size walks the region backwards without a
 * separate footer; the free list threads through free payloads.
 */
struct qk_block {
    u32 prev_size;
    u32 size;
    u32 used;
    u32 magic;
};

struct qk_links {
    struct qk_block *prev;
    struct qk_block *next;
};

static struct {
    unsigned char *base;
    size_t bytes;
    struct qk_block *free_list;
    unsigned char *reserve;
    u32 reserve_bytes;
    int exhausted;
    struct qk_arena_stats stats;
} arena;

static size_t qk_round_up(size_t value, size_t multiple)
{
    return (value + multiple - 1) & ~(multiple - 1);
}

static unsigned char *qk_payload(struct qk_block *block)
{
    return (unsigned char *)block + sizeof(struct qk_block);
}

static struct qk_links *qk_links_of(struct qk_block *block)
{
    return (struct qk_links *)qk_payload(block);
}

static struct qk_block *qk_next_block(struct qk_block *block)
{
    unsigned char *next = qk_payload(block) + block->size;

    return next < arena.base + arena.bytes ? (struct qk_block *)next : NULL;
}

static struct qk_block *qk_prev_block(struct qk_block *block)
{
    if (!block->prev_size)
        return NULL;
    return (struct qk_block *)((unsigned char *)block - sizeof(struct qk_block) -
                               block->prev_size);
}

static void qk_free_list_insert(struct qk_block *block)
{
    struct qk_links *links = qk_links_of(block);

    links->prev = NULL;
    links->next = arena.free_list;
    if (arena.free_list)
        qk_links_of(arena.free_list)->prev = block;
    arena.free_list = block;
}

static void qk_free_list_remove(struct qk_block *block)
{
    struct qk_links *links = qk_links_of(block);

    if (links->prev)
        qk_links_of(links->prev)->next = links->next;
    else
        arena.free_list = links->next;
    if (links->next)
        qk_links_of(links->next)->prev = links->prev;
    links->prev = NULL;
    links->next = NULL;
}

int qk_arena_init(void *memory, size_t bytes)
{
    struct qk_block *block;

    if (!memory || ((unsigned long)memory & (QK_ALIGN - 1)) ||
        bytes < sizeof(struct qk_block) + QK_MIN_PAYLOAD || bytes > 0xf0000000u)
        return -22; /* -EINVAL */
    memset(&arena, 0, sizeof(arena));
    arena.base = (unsigned char *)memory;
    arena.bytes = bytes & ~(size_t)(QK_ALIGN - 1);
    /* Reserve a sixteenth of the region, bounded, for the overflow path. */
    arena.reserve_bytes = (u32)(arena.bytes / 16);
    if (arena.reserve_bytes > QK_ARENA_RESERVE_MAX)
        arena.reserve_bytes = QK_ARENA_RESERVE_MAX;
    if (arena.reserve_bytes < QK_MIN_PAYLOAD)
        arena.reserve_bytes = QK_MIN_PAYLOAD;
    if (arena.bytes < (size_t)arena.reserve_bytes + sizeof(struct qk_block) +
                      QK_MIN_PAYLOAD) {
        arena.reserve_bytes = 0;
    } else {
        arena.bytes -= arena.reserve_bytes;
        arena.reserve = arena.base + arena.bytes;
    }
    block = (struct qk_block *)arena.base;
    block->prev_size = 0;
    block->size = (u32)(arena.bytes - sizeof(struct qk_block));
    block->used = 0;
    block->magic = QK_MAGIC_FREE;
    arena.free_list = NULL;
    qk_free_list_insert(block);
    arena.stats.capacity = block->size;
    arena.stats.largest_free = block->size;
    arena.stats.reserve = arena.reserve_bytes;
    return 0;
}

static void qk_refresh_largest_free(void)
{
    struct qk_block *block = arena.free_list;
    u32 largest = 0;

    for (; block; block = qk_links_of(block)->next)
        if (block->size > largest)
            largest = block->size;
    arena.stats.largest_free = largest;
}

/* Last resort: hand out the reserve block instead of a NULL that ImGui would
 * dereference. Concurrent users of it overlap by design; the caller drops the
 * frame as soon as it sees qk_arena_exhausted().
 */
static void *qk_overflow(size_t wanted)
{
    arena.stats.failures++;
    if (!arena.reserve || wanted > arena.reserve_bytes)
        return NULL;
    arena.exhausted = 1;
    arena.stats.overflows++;
    return arena.reserve;
}

void *qk_arena_alloc(size_t bytes)
{
    size_t wanted;
    struct qk_block *block, *split, *after;

    if (!arena.base || !bytes)
        return NULL;
    wanted = qk_round_up(bytes, QK_ALIGN);
    if (wanted < QK_MIN_PAYLOAD)
        wanted = QK_MIN_PAYLOAD;
    if (wanted > arena.stats.capacity)
        return qk_overflow(wanted);
    for (block = arena.free_list; block; block = qk_links_of(block)->next)
        if (block->size >= wanted)
            break;
    if (!block)
        return qk_overflow(wanted);
    qk_free_list_remove(block);
    /* Split only when the tail can hold a block of its own. */
    if (block->size >= wanted + sizeof(struct qk_block) + QK_MIN_PAYLOAD) {
        split = (struct qk_block *)(qk_payload(block) + wanted);
        split->prev_size = (u32)wanted;
        split->size = block->size - (u32)wanted - (u32)sizeof(struct qk_block);
        split->used = 0;
        split->magic = QK_MAGIC_FREE;
        block->size = (u32)wanted;
        after = qk_next_block(split);
        if (after)
            after->prev_size = split->size;
        qk_free_list_insert(split);
    }
    block->used = 1;
    block->magic = QK_MAGIC_USED;
    arena.stats.in_use += block->size;
    arena.stats.blocks++;
    arena.stats.allocations++;
    if (arena.stats.in_use > arena.stats.peak)
        arena.stats.peak = arena.stats.in_use;
    qk_refresh_largest_free();
    return qk_payload(block);
}

void qk_arena_free(void *pointer)
{
    struct qk_block *block, *neighbour, *after;

    if (!pointer || !arena.base)
        return;
    if (pointer == arena.reserve)
        return; /* The reserve block is never on the free list. */
    block = (struct qk_block *)((unsigned char *)pointer - sizeof(struct qk_block));
    if ((unsigned char *)block < arena.base ||
        (unsigned char *)block >= arena.base + arena.bytes ||
        block->magic != QK_MAGIC_USED || !block->used)
        return; /* Double free or a foreign pointer: leave the region intact. */
    arena.stats.in_use -= block->size;
    arena.stats.blocks--;
    block->used = 0;
    block->magic = QK_MAGIC_FREE;

    neighbour = qk_next_block(block);
    if (neighbour && !neighbour->used) {
        qk_free_list_remove(neighbour);
        block->size += neighbour->size + (u32)sizeof(struct qk_block);
        after = qk_next_block(block);
        if (after)
            after->prev_size = block->size;
    }
    neighbour = qk_prev_block(block);
    if (neighbour && !neighbour->used) {
        qk_free_list_remove(neighbour);
        neighbour->size += block->size + (u32)sizeof(struct qk_block);
        after = qk_next_block(neighbour);
        if (after)
            after->prev_size = neighbour->size;
        block = neighbour;
    }
    qk_free_list_insert(block);
    qk_refresh_largest_free();
}

void qk_arena_get_stats(struct qk_arena_stats *stats)
{
    if (stats)
        *stats = arena.stats;
}

int qk_arena_exhausted(void)
{
    return arena.exhausted;
}

int qk_arena_check(void)
{
    struct qk_block *block = (struct qk_block *)arena.base;
    u32 previous = 0, in_use = 0, blocks = 0;

    if (!arena.base)
        return -22;
    while (block) {
        if (block->magic != (block->used ? QK_MAGIC_USED : QK_MAGIC_FREE))
            return -84; /* -EILSEQ */
        if (block->prev_size != previous || !block->size ||
            block->size & (QK_ALIGN - 1))
            return -84;
        if (qk_payload(block) + block->size > arena.base + arena.bytes)
            return -84;
        if (block->used) {
            in_use += block->size;
            blocks++;
        }
        previous = block->size;
        block = qk_next_block(block);
    }
    if (in_use != arena.stats.in_use || blocks != arena.stats.blocks)
        return -84;
    return 0;
}
