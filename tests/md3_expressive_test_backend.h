#pragma once
#include "imgui_internal.h"

// One test-engine hook implementation is linked by md3_expressive_test.cpp.
// Individual suites can temporarily observe real ItemAdd calls without defining
// duplicate global ImGui hooks or reproducing component layout calculations.
namespace md3expressivetest {
using ItemObserver = void (*)(ImGuiContext*, ImGuiID, const ImRect&,
                             const ImGuiLastItemData*);
extern ItemObserver item_observer;
struct ScopedItemObserver {
    ItemObserver previous;
    explicit ScopedItemObserver(ItemObserver observer) : previous(item_observer) {
        item_observer = observer;
    }
    ~ScopedItemObserver() { item_observer = previous; }
    ScopedItemObserver(const ScopedItemObserver&) = delete;
    ScopedItemObserver& operator=(const ScopedItemObserver&) = delete;
};
} // namespace md3expressivetest
