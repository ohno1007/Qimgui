#include "core/crash_diagnostics.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include <thread>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static void ReleaseInput() {
    const char message[] = "[cleanup] input released\n";
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
}
static void PreviousHandler(int signal, siginfo_t* info, void*) {
    _exit(signal == SIGSEGV && info != nullptr ? 77 : 78);
}
static void Check(bool worker, bool chain, const char* lateModule = nullptr) {
    int pipefd[2]; assert(pipe(pipefd) == 0);
    pid_t child = fork(); assert(child >= 0);
    const int signal = lateModule ? SIGILL : worker ? SIGABRT : SIGSEGV;
    if (child == 0) {
        close(pipefd[0]); assert(dup2(pipefd[1], STDERR_FILENO) >= 0); close(pipefd[1]);
        struct rlimit noCore{0,0}; setrlimit(RLIMIT_CORE, &noCore);
        if (chain) {
            struct sigaction previous{};
            previous.sa_sigaction = PreviousHandler; previous.sa_flags = SA_SIGINFO;
            sigemptyset(&previous.sa_mask); assert(sigaction(signal, &previous, nullptr) == 0);
        }
        aimgui::diagnostics::InstallCrashHandler(ReleaseInput);
        aimgui::diagnostics::InstallCrashHandler(ReleaseInput);
        aimgui::diagnostics::SetStage("sensor-events-test");
        if (lateModule) {
            void* library = dlopen(lateModule, RTLD_NOW | RTLD_LOCAL);
            assert(library);
            auto trap = reinterpret_cast<void (*)()>(dlsym(library, "DiagnosticTestTrap"));
            assert(trap);
            aimgui::diagnostics::RefreshLoadedModules();
            aimgui::diagnostics::RefreshLoadedModules();
            trap();
            _exit(98);
        }
        if (worker) { std::thread t([signal]{raise(signal);}); t.join(); }
        else raise(signal);
        _exit(99);
    }
    close(pipefd[1]); std::string output; char chunk[256]; ssize_t count;
    while ((count = read(pipefd[0], chunk, sizeof(chunk))) > 0) output.append(chunk, count);
    close(pipefd[0]); int status; assert(waitpid(child, &status, 0) == child);
    if (chain) assert(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    else assert(WIFSIGNALED(status) && WTERMSIG(status) == signal);
    assert(output.find("[crash] signal=" + std::to_string(signal)) != std::string::npos);
    assert(output.find(" pc=0x") != std::string::npos);
    assert(output.find("image_base=0x") != std::string::npos);
    assert(output.find("main_stage=sensor-events-test") != std::string::npos);
    const size_t tidStart = output.find(" tid=");
    assert(tidStart != std::string::npos);
    const int tid = std::stoi(output.substr(tidStart + 5));
    assert(worker ? tid != child : tid == child);
    assert(output.find(" main_tid=" + std::to_string(child)) != std::string::npos);
    assert(output.find(lateModule ? " pc_module=libcrash-diagnostics-test-module.so"
                                  : " pc_module=libc.so") != std::string::npos);
    assert(output.find(" pc_module_offset=0x") != std::string::npos);
    assert(output.find("[cleanup] input released") != std::string::npos);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Check(false, false); Check(true, false); Check(false, true);
    Check(false, false, argv[1]);
    std::puts("PASS: crash diagnostics identify libc/late-loaded modules and main/worker TIDs, release input and preserve previous handlers");
}
