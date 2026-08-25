#pragma once

#include <CubismFramework.hpp>
#include <ICubismAllocator.hpp>
#include <cstdlib>

namespace aimgui {
namespace live2d {

class Allocator : public Csm::ICubismAllocator {
public:
    void* Allocate(const Csm::csmSizeType size) override {
        return std::malloc(size);
    }
    void Deallocate(void* memory) override {
        std::free(memory);
    }
    void* AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override {

        size_t offset = alignment + sizeof(void*);
        void* p = std::malloc(size + offset);
        if (!p) return nullptr;
        char* aligned = reinterpret_cast<char*>(
            (reinterpret_cast<size_t>(p) + offset) & ~(static_cast<size_t>(alignment) - 1));
        reinterpret_cast<void**>(aligned)[-1] = p;
        return aligned;
    }
    void DeallocateAligned(void* alignedMemory) override {
        if (alignedMemory)
            std::free(reinterpret_cast<void**>(alignedMemory)[-1]);
    }
};

}
}
