/* SPDX-License-Identifier: MIT */
/* The slice of the C++ runtime these sources need. The kernel links no
 * libstdc++, and the module is built with -fno-exceptions, -fno-rtti and
 * -fno-threadsafe-statics, so what remains is allocation, a few ABI symbols,
 * and the libc entry points the containers reach for.
 */
#include <stddef.h>

#include "qk_arena.h"
#include "qk_math.h"

extern "C" {

void *malloc(size_t bytes)
{
    return qk_arena_alloc(bytes);
}

void free(void *pointer)
{
    qk_arena_free(pointer);
}

void *calloc(size_t count, size_t bytes)
{
    const size_t total = count * bytes;
    unsigned char *area;
    size_t i;

    if (count && total / count != bytes)
        return nullptr;
    area = static_cast<unsigned char *>(qk_arena_alloc(total));
    if (!area)
        return nullptr;
    for (i = 0; i < total; i++)
        area[i] = 0;
    return area;
}

/* The arena has no block-size query, so this copies conservatively: callers
 * here only ever grow small buffers, and ImGui does not use realloc at all.
 */
void *realloc(void *pointer, size_t bytes)
{
    void *grown;

    if (!pointer)
        return qk_arena_alloc(bytes);
    if (!bytes) {
        qk_arena_free(pointer);
        return nullptr;
    }
    grown = qk_arena_alloc(bytes);
    if (!grown)
        return nullptr;
    __builtin_memcpy(grown, pointer, bytes);
    qk_arena_free(pointer);
    return grown;
}

/* Insertion sort: stb_rect_pack sorts a few hundred glyph rectangles once per
 * atlas build, and the kernel's own sort() is not worth the indirection.
 */
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *))
{
    unsigned char *items = static_cast<unsigned char *>(base);
    size_t i, j, byte;

    for (i = 1; i < count; i++) {
        for (j = i; j > 0; j--) {
            unsigned char *left = items + (j - 1) * size;
            unsigned char *right = items + j * size;

            if (compare(left, right) <= 0)
                break;
            for (byte = 0; byte < size; byte++) {
                const unsigned char held = left[byte];

                left[byte] = right[byte];
                right[byte] = held;
            }
        }
    }
}

/* A pure virtual call means the object is being used during construction or
 * after destruction. Returning is wrong, but so is taking the machine down.
 */
void __cxa_pure_virtual(void)
{
}

int __cxa_atexit(void (*destructor)(void *), void *argument, void *owner)
{
    (void)destructor;
    (void)argument;
    (void)owner;
    /* Nothing here outlives the module, and the module never unloads once the
     * display hook is installed.
     */
    return 0;
}

void *__dso_handle = nullptr;

} // extern "C"

void *operator new(size_t bytes)
{
    return qk_arena_alloc(bytes ? bytes : 1);
}

void *operator new[](size_t bytes)
{
    return qk_arena_alloc(bytes ? bytes : 1);
}

void operator delete(void *pointer) noexcept
{
    qk_arena_free(pointer);
}

void operator delete[](void *pointer) noexcept
{
    qk_arena_free(pointer);
}

void operator delete(void *pointer, size_t) noexcept
{
    qk_arena_free(pointer);
}

void operator delete[](void *pointer, size_t) noexcept
{
    qk_arena_free(pointer);
}
