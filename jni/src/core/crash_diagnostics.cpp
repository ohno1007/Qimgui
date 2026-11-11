#include "crash_diagnostics.h"
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <link.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

namespace aimgui::diagnostics {
namespace {
std::atomic<const char*> stage{"startup"};
static_assert(decltype(stage)::is_always_lock_free);
struct sigaction previous[NSIG]{};
void (*releaseInput)() = nullptr;
uintptr_t imageBase = 0, imageStart = 0, imageEnd = 0;
pid_t mainTid = 0;
struct Module {
    uintptr_t base, start, end;
    char name[160];
};
Module modules[256]{};
std::atomic<unsigned> moduleCount{0};
static_assert(decltype(moduleCount)::is_always_lock_free);
std::atomic_flag refreshingModules = ATOMIC_FLAG_INIT;
alignas(16) unsigned char alternateStack[64 * 1024];
bool installed = false;

// Signal-safe formatting: no allocation, stdio, symbol lookup or unwinding.
struct Line {
    char data[1024];
    size_t size = 0;
    void Text(const char* value) {
        if (value) while (*value && size < sizeof(data)) data[size++] = *value++;
    }
    void Hex(uintptr_t value) {
        Text("0x"); char digits[2 * sizeof(value)]; unsigned count = 0;
        do { digits[count++] = "0123456789abcdef"[value & 15]; value >>= 4; } while (value);
        while (count && size < sizeof(data)) data[size++] = digits[--count];
    }
    void Decimal(int value) {
        if (value < 0) Text("-");
        unsigned magnitude = value < 0 ? 0u - unsigned(value) : unsigned(value);
        char digits[16]; unsigned count = 0;
        do { digits[count++] = char('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
        while (count && size < sizeof(data)) data[size++] = digits[--count];
    }
};

int RecordModule(dl_phdr_info* info, size_t, void*) {
    uintptr_t start = UINTPTR_MAX, end = 0;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD) continue;
        const uintptr_t low = info->dlpi_addr + segment.p_vaddr;
        if (low < start) start = low;
        if (low + segment.p_memsz > end) end = low + segment.p_memsz;
    }
    if (start >= end) return 0;
    const char* path = info->dlpi_name;
    const char* name = path && *path ? path : "<executable>";
    if (const char* slash = std::strrchr(name, '/')) name = slash + 1;
    Module next{info->dlpi_addr, start, end, {}};
    std::strncpy(next.name, name, sizeof(next.name) - 1);
    const unsigned count = moduleCount.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < count; ++i) {
        const auto& old = modules[i];
        if (old.base == next.base && old.start == start && old.end == end &&
            std::strcmp(old.name, next.name) == 0) return 0;
    }
    if (count == sizeof(modules) / sizeof(modules[0])) return 1;
    // A signal can arrive at any point. Publish only a fully copied record,
    // and never overwrite an entry visible to a signal on another thread.
    modules[count] = next;
    moduleCount.store(count + 1, std::memory_order_release);
    return 0;
}

void DescribeModule(Line& line, uintptr_t address, const char* label, const char* offsetLabel) {
    if (!address) return;
    line.Text(label);
    // Prefer the newest record if an unloaded library's address was reused.
    unsigned count = moduleCount.load(std::memory_order_acquire);
    while (count) {
        const auto& module = modules[--count];
        if (address < module.start || address >= module.end) continue;
        line.Text(module.name);
        line.Text(offsetLabel); line.Hex(address - module.base);
        return;
    }
    line.Text("unknown");
}

int FindImage(dl_phdr_info* info, size_t, void*) {
    const uintptr_t here = reinterpret_cast<uintptr_t>(&InstallCrashHandler);
    uintptr_t start = UINTPTR_MAX, end = 0;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD) continue;
        const uintptr_t low = info->dlpi_addr + segment.p_vaddr;
        if (low < start) start = low;
        if (low + segment.p_memsz > end) end = low + segment.p_memsz;
    }
    if (here < start || here >= end) return 0;
    imageBase = info->dlpi_addr; imageStart = start; imageEnd = end;
    return 1;
}

void OnSignal(int signal, siginfo_t* info, void* context) {
    uintptr_t pc = 0, lr = 0;
    auto* registers = static_cast<ucontext_t*>(context);
#if defined(__aarch64__)
    pc = registers->uc_mcontext.pc;
    lr = registers->uc_mcontext.regs[30];
#elif defined(__x86_64__)
    pc = registers->uc_mcontext.gregs[REG_RIP];
#endif
    Line line;
    line.Text("[crash] signal="); line.Decimal(signal);
    line.Text(" code="); line.Decimal(info ? info->si_code : 0);
    line.Text(" fault="); line.Hex(info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0);
    line.Text(" pc="); line.Hex(pc);
    line.Text(" lr="); line.Hex(lr);
    line.Text(" image_base="); line.Hex(imageBase);
    if (pc >= imageStart && pc < imageEnd) {
        line.Text(" pc_offset="); line.Hex(pc - imageBase);
    }
    if (lr >= imageStart && lr < imageEnd) {
        line.Text(" lr_offset="); line.Hex(lr - imageBase);
    }
    line.Text(" main_stage="); line.Text(stage.load(std::memory_order_relaxed));
    line.Text(" tid="); line.Decimal(static_cast<int>(syscall(SYS_gettid)));
    line.Text(" main_tid="); line.Decimal(mainTid);
    DescribeModule(line, pc, " pc_module=", " pc_module_offset=");
    DescribeModule(line, lr, " lr_module=", " lr_module_offset=");
    line.Text("\n");
    const ssize_t ignored = write(STDERR_FILENO, line.data, line.size);
    (void)ignored;
    if (releaseInput) releaseInput();
    // Restore Android's debuggerd handler so native tombstones are preserved.
    // Re-raised signals are delivered after our handler returns.
    struct sigaction restore = previous[signal];
    if (restore.sa_handler == SIG_IGN) restore.sa_handler = SIG_DFL;
    sigaction(signal, &restore, nullptr);
    raise(signal);
}
}

void SetStage(const char* value) { stage.store(value, std::memory_order_relaxed); }

void RefreshLoadedModules() {
    // Refreshes may race each other; signals never acquire this guard.
    if (refreshingModules.test_and_set(std::memory_order_acquire)) return;
    dl_iterate_phdr(RecordModule, nullptr);
    refreshingModules.clear(std::memory_order_release);
}

void InstallCrashHandler(void (*release_input)()) {
    if (installed) return;
    releaseInput = release_input;
    mainTid = static_cast<pid_t>(syscall(SYS_gettid));
    dl_iterate_phdr(FindImage, nullptr);
    RefreshLoadedModules();
    stack_t current{};
    if (sigaltstack(nullptr, &current) == 0 && (current.ss_flags & SS_DISABLE)) {
        stack_t replacement{};
        replacement.ss_sp = alternateStack;
        replacement.ss_size = sizeof(alternateStack);
        sigaltstack(&replacement, nullptr);
    }
    struct sigaction action{};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGINT};
    for (int signal : signals) sigaction(signal, &action, &previous[signal]);
    installed = true;
}
}
