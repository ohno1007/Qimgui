#include "java_bridge.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aimgui::java_bridge {
namespace {
constexpr size_t kMaxReply = 96 * 1024;
constexpr size_t kMaxText = 64 * 1024;
std::mutex g_mutex;
std::string g_error, g_socket, g_token;
pid_t g_host = -1;
using Clock = std::chrono::steady_clock;

struct Fd {
    int value;
    explicit Fd(int fd) : value(fd) {}
    ~Fd() { if (value >= 0) close(value); }
};

bool Error(const char* message) { g_error = message; return false; }

std::string DexPath() {
    char path[4096];
    const ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (size <= 0) return {};
    path[size] = '\0';
    std::string result(path);
    return result.substr(0, result.find_last_of('/') + 1) + "ime-host.dex";
}

bool Credentials() {
    if (!g_token.empty()) return true;
    Fd random(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (random.value < 0) return Error("cannot open random source");
    unsigned char bytes[32];
    size_t done = 0;
    while (done < sizeof(bytes)) {
        ssize_t n = read(random.value, bytes + done, sizeof(bytes) - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return Error("cannot generate host credentials");
        done += static_cast<size_t>(n);
    }
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char b : bytes) {
        g_token += hex[b >> 4];
        g_token += hex[b & 15];
    }
    g_socket = "aimgui-ime-" + std::to_string(getpid()) + "-" + g_token.substr(0, 16);
    return true;
}

bool Wait(int fd, short events, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        pollfd p{fd, events, 0};
        int rc = poll(&p, 1, static_cast<int>(std::max<int64_t>(ms, 1)));
        if (rc < 0 && errno == EINTR) continue;
        if (rc > 0 && (p.revents & events)) return true;
        return Error("Java host timeout or disconnected");
    }
    return Error("Java host timeout");
}

int Connect() {
    if (g_socket.empty()) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    // LocalServerSocket(String) uses Linux's ABSTRACT namespace, not a file.
    std::memcpy(address.sun_path + 1, g_socket.data(), g_socket.size());
    const auto size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + g_socket.size());
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void StopHost() {
    if (g_host <= 0) return;
    // Reap/check our own child before signalling, avoiding PID reuse.
    int status;
    pid_t result;
    do { result = waitpid(g_host, &status, WNOHANG); } while (result < 0 && errno == EINTR);
    if (result == 0) {
        kill(g_host, SIGKILL);
        while (waitpid(g_host, &status, 0) < 0 && errno == EINTR) {}
    }
    g_host = -1;
}

int HostFd() {
    int fd = Connect();
    if (fd >= 0) return fd;
    StopHost();
    const auto dex = DexPath();
    if (dex.empty() || access(dex.c_str(), R_OK) != 0) {
        Error("ime-host.dex missing next to AImGui; build and deploy the Java helper");
        return -1;
    }
    if (!Credentials()) return -1;
    const std::string classpath = "-Djava.class.path=" + dex;
    const std::string parent = std::to_string(getpid());
    g_host = fork();
    if (g_host == 0) {
        execl("/system/bin/app_process", "app_process", classpath.c_str(),
              "/system/bin", "ImeHost", g_socket.c_str(), g_token.c_str(),
              parent.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    if (g_host < 0) { Error("cannot start Java host"); return -1; }
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    do {
        fd = Connect();
        if (fd >= 0) return fd;
        poll(nullptr, 0, 20);
    } while (Clock::now() < deadline);
    StopHost();
    Error("Java host failed to start");
    return -1;
}

bool Command(const std::string& command, std::string* output = nullptr) {
    Fd fd(HostFd());
    if (fd.value < 0) return false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(750);
    const std::string request = g_token + " " + command + "\n";
    size_t sent = 0;
    while (sent < request.size()) {
        if (!Wait(fd.value, POLLOUT, deadline)) return false;
        ssize_t n = send(fd.value, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return Error("Java host write failed");
        sent += static_cast<size_t>(n);
    }
    std::string reply;
    while (reply.size() <= kMaxReply) {
        if (!Wait(fd.value, POLLIN, deadline)) return false;
        char buffer[1024];
        ssize_t n = read(fd.value, buffer, sizeof(buffer));
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) return Error("Java host closed without a reply");
        reply.append(buffer, static_cast<size_t>(n));
        size_t newline = reply.find('\n');
        if (newline == std::string::npos) continue;
        reply.resize(newline);
        if (reply.rfind("ERR ", 0) == 0) return Error(reply.c_str() + 4);
        if (reply != "OK" && reply.rfind("OK ", 0) != 0) return Error("invalid Java host reply");
        if (output) *output = reply.size() > 3 ? reply.substr(3) : "";
        g_error.clear();
        return true;
    }
    return Error("Java host reply too large");
}

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string Encode(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); i += 3) {
        unsigned v = static_cast<unsigned char>(text[i]) << 16;
        if (i + 1 < text.size()) v |= static_cast<unsigned char>(text[i + 1]) << 8;
        if (i + 2 < text.size()) v |= static_cast<unsigned char>(text[i + 2]);
        result += kBase64[(v >> 18) & 63]; result += kBase64[(v >> 12) & 63];
        result += i + 1 < text.size() ? kBase64[(v >> 6) & 63] : '=';
        result += i + 2 < text.size() ? kBase64[v & 63] : '=';
    }
    return result;
}

bool Decode(const std::string& encoded, std::string* output) {
    output->clear();
    unsigned value = 0;
    int bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        const char* pos = c ? std::strchr(kBase64, c) : nullptr;
        if (!pos) return Error("invalid base64 from Java host");
        value = (value << 6) | static_cast<unsigned>(pos - kBase64);
        bits += 6;
        if (bits >= 0) { output->push_back(static_cast<char>((value >> bits) & 255)); bits -= 8; }
    }
    return true;
}
}

bool Available() { return access(DexPath().c_str(), R_OK) == 0; }
const char* LastError() {
    thread_local std::string copy;
    std::lock_guard<std::mutex> lock(g_mutex);
    copy = g_error;
    return copy.c_str();
}
bool SetClipboard(const char* text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string value = text ? text : "";
    return value.size() <= kMaxText ? Command("CLIPSET " + Encode(value)) : Error("clipboard text too large");
}
bool GetClipboard(std::string* text) {
    if (!text) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string reply;
    return Command("CLIPGET", &reply) && Decode(reply, text);
}
bool ShowInputMethod(const char* initial) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string value = initial ? initial : "";
    return value.size() <= kMaxText ? Command("SHOW " + Encode(value)) : Error("input text too large");
}
bool GetInputText(std::string* text) {
    if (!text) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string reply;
    return Command("GET", &reply) && Decode(reply, text);
}
bool HideInputMethod() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return Command("HIDE");
}
void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    StopHost();
    g_socket.clear(); g_token.clear();
}
}
