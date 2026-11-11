// The frame allocator runs with preemption disabled and cannot fall back to
// the kernel allocators, so it has to survive ImGui's allocation pattern
// without fragmenting into uselessness. This exercises it on the host.
#include "qk_arena.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr size_t kRegion = 1u << 20;
alignas(16) unsigned char region[kRegion];

struct Block {
    unsigned char* pointer;
    size_t bytes;
    unsigned char pattern;
};

// Deterministic sequence: a failure reproduces on every run and every host.
u32 Random(u32& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

void Fill(Block& block) {
    std::memset(block.pointer, block.pattern, block.bytes);
}

void Verify(const Block& block) {
    for (size_t i = 0; i < block.bytes; i++)
        assert(block.pointer[i] == block.pattern);
}

void Basics() {
    assert(qk_arena_init(nullptr, kRegion) < 0);
    assert(qk_arena_init(region + 1, kRegion - 1) < 0);   // Misaligned.
    assert(qk_arena_init(region, 8) < 0);                 // Too small for a block.
    assert(qk_arena_init(region, kRegion) == 0);

    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    // Capacity is the region minus the reserve block and one header.
    assert(stats.reserve > 0 && stats.capacity + stats.reserve <= kRegion);
    assert(stats.capacity + stats.reserve > kRegion - 64);
    assert(stats.in_use == 0 && stats.blocks == 0 && stats.largest_free == stats.capacity);

    assert(qk_arena_alloc(0) == nullptr);
    void* first = qk_arena_alloc(1);
    assert(first && ((unsigned long)first & 15) == 0);
    qk_arena_get_stats(&stats);
    assert(stats.blocks == 1 && stats.in_use >= 16 && stats.allocations == 1);
    assert(qk_arena_check() == 0);

    // Freeing the only block gives the whole region back.
    qk_arena_free(first);
    qk_arena_get_stats(&stats);
    assert(stats.blocks == 0 && stats.in_use == 0);
    assert(stats.largest_free == stats.capacity);
    assert(qk_arena_check() == 0);

    // A double free, a null free and a foreign pointer are all ignored.
    qk_arena_free(first);
    qk_arena_free(nullptr);
    unsigned char outside[64] = {};
    qk_arena_free(outside + 32);
    assert(qk_arena_check() == 0);
    qk_arena_get_stats(&stats);
    assert(stats.blocks == 0 && stats.in_use == 0);
}

void Alignment() {
    assert(qk_arena_init(region, kRegion) == 0);
    std::vector<void*> blocks;
    for (size_t bytes = 1; bytes <= 300; bytes++) {
        void* pointer = qk_arena_alloc(bytes);
        assert(pointer && ((unsigned long)pointer & 15) == 0);
        blocks.push_back(pointer);
    }
    assert(qk_arena_check() == 0);
    for (void* pointer : blocks)
        qk_arena_free(pointer);
    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    assert(stats.in_use == 0 && stats.largest_free == stats.capacity);
}

// Exhaustion falls back to the reserve block rather than returning NULL, which
// ImGui would dereference. The region must stay walkable either way.
void Exhaustion() {
    assert(qk_arena_init(region, kRegion) == 0);
    assert(!qk_arena_exhausted());
    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    const u32 reserve = stats.reserve;
    assert(reserve >= 32 && stats.capacity + reserve <= kRegion);

    std::vector<void*> blocks;
    void* overflow = nullptr;
    for (int round = 0; round < 10000; round++) {
        void* pointer = qk_arena_alloc(4096);
        assert(pointer);
        if (qk_arena_exhausted()) {
            overflow = pointer;
            break;
        }
        blocks.push_back(pointer);
    }
    assert(overflow && blocks.size() >= 200);
    // The reserve is writable and lies outside every live block.
    std::memset(overflow, 0x5a, 4096);
    for (void* pointer : blocks)
        assert(pointer != overflow);
    // Repeated overflow hands out the same block instead of failing.
    assert(qk_arena_alloc(4096) == overflow);
    qk_arena_get_stats(&stats);
    assert(stats.overflows >= 2 && stats.failures >= 2);
    assert(qk_arena_check() == 0);

    // Freeing the reserve is a no-op; freeing the rest restores the region.
    qk_arena_free(overflow);
    for (void* pointer : blocks)
        qk_arena_free(pointer);
    qk_arena_get_stats(&stats);
    assert(stats.in_use == 0 && stats.largest_free == stats.capacity);
    assert(qk_arena_check() == 0);

    // A request too big for the reserve has nowhere to go and says so.
    assert(qk_arena_init(region, kRegion) == 0);
    assert(qk_arena_alloc(kRegion * 2) == nullptr);
    assert(!qk_arena_exhausted() && qk_arena_check() == 0);
}

// ImGui grows ImVector by allocating the bigger buffer, copying, then freeing
// the old one: the allocator must reuse the hole left behind.
void Growth() {
    assert(qk_arena_init(region, kRegion) == 0);
    size_t capacity = 64;
    unsigned char* buffer = (unsigned char*)qk_arena_alloc(capacity);
    assert(buffer);
    std::memset(buffer, 0xa5, capacity);
    for (int step = 0; step < 12; step++) {
        const size_t wanted = capacity * 2;
        unsigned char* grown = (unsigned char*)qk_arena_alloc(wanted);
        assert(grown);
        std::memcpy(grown, buffer, capacity);
        qk_arena_free(buffer);
        buffer = grown;
        capacity = wanted;
        for (size_t i = 0; i < 64; i++)
            assert(buffer[i] == 0xa5);
        assert(qk_arena_check() == 0);
    }
    qk_arena_free(buffer);
    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    assert(stats.in_use == 0 && stats.largest_free == stats.capacity);
}

void Stress() {
    assert(qk_arena_init(region, kRegion) == 0);
    std::vector<Block> live;
    u32 state = 12345;
    size_t peak_live = 0;

    for (int round = 0; round < 60000; round++) {
        const bool allocate = live.empty() || (Random(state) % 100) < 55;
        if (allocate) {
            const size_t bytes = 1 + Random(state) % 3000;
            struct qk_arena_stats before{};
            qk_arena_get_stats(&before);
            unsigned char* pointer = (unsigned char*)qk_arena_alloc(bytes);
            struct qk_arena_stats after{};
            qk_arena_get_stats(&after);
            // A block served from the reserve overlaps other reserve users, so
            // it is not a live allocation to track.
            if (!pointer || after.overflows != before.overflows)
                continue;
            Block block{pointer, bytes, (unsigned char)(round & 0xff)};
            Fill(block);
            live.push_back(block);
            peak_live = live.size() > peak_live ? live.size() : peak_live;
        } else {
            const size_t index = Random(state) % live.size();
            Verify(live[index]);
            qk_arena_free(live[index].pointer);
            live[index] = live.back();
            live.pop_back();
        }
        if (round % 500 == 0) {
            // Live blocks never overlap, so every pattern is still intact.
            for (const Block& block : live)
                Verify(block);
            assert(qk_arena_check() == 0);
        }
    }
    for (const Block& block : live) {
        Verify(block);
        qk_arena_free(block.pointer);
    }
    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    assert(peak_live > 50);
    assert(stats.in_use == 0 && stats.blocks == 0);
    // Churn must not have leaked the region into the reserve path alone.
    assert(stats.allocations > stats.overflows * 4);
    // Full coalescing: after the churn the region is one free block again.
    assert(stats.largest_free == stats.capacity);
    assert(stats.peak > 0 && stats.allocations > 1000);
    assert(qk_arena_check() == 0);
    std::printf("kernel arena: %llu allocations, %llu failures, peak %u bytes\n",
                (unsigned long long)stats.allocations,
                (unsigned long long)stats.failures, stats.peak);
}

} // namespace

int main() {
    Basics();
    Alignment();
    Exhaustion();
    Growth();
    Stress();
    std::printf("kernel arena: all checks passed\n");
    return 0;
}
