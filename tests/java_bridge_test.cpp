// Exercise the public bridge against a real abstract Unix socket, without Android.
#include "../jni/src/core/java_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

namespace bridge = aimgui::java_bridge;

static void WithHost(const std::string& reply, const std::function<void()>& client, int delay = 0) {
    assert(bridge::Credentials());
    bridge::Fd server(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    assert(server.value >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path + 1, bridge::g_socket.data(), bridge::g_socket.size());
    assert(bind(server.value, reinterpret_cast<sockaddr*>(&address),
        offsetof(sockaddr_un,sun_path)+1+bridge::g_socket.size()) == 0);
    assert(listen(server.value,1) == 0);
    std::thread worker([&] {
        bridge::Fd fd(accept(server.value,nullptr,nullptr));
        assert(fd.value >= 0);
        std::string request;
        char byte;
        while (read(fd.value,&byte,1) == 1 && byte != '\n') request += byte;
        assert(request.rfind(bridge::g_token + " ",0) == 0);
        if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (!reply.empty()) send(fd.value,reply.data(),reply.size(),MSG_NOSIGNAL);
    });
    client(); worker.join();
}

int main() {
    // Pure conversion checks still run where the host denies socket creation.
    std::string decoded;
    assert(bridge::Encode("hello") == "aGVsbG8=");
    assert(bridge::Decode("aGVsbG8=", &decoded) && decoded == "hello");
    assert(!bridge::Decode("!", &decoded));
    bridge::Fd probe(socket(AF_UNIX, SOCK_STREAM, 0));
    if (probe.value < 0) {
        std::perror("SKIP: host does not permit Unix sockets");
        return std::getenv("AIMGUI_REQUIRE_IPC_TEST") ? 1 : 77;
    }
    const std::string unicode = u8"中文🙂\nsecond line";
    WithHost("OK\n",[&] { assert(bridge::SetClipboard(unicode.c_str())); });
    WithHost("OK " + bridge::Encode(unicode) + "\n",[&] {
        std::string result;
        assert(bridge::GetClipboard(&result) && result == unicode);
    });
    WithHost("ERR unavailable\n",[] { assert(!bridge::SetClipboard("test")); });
    WithHost("unexpected\n",[] { assert(!bridge::HideInputMethod()); });
    WithHost("",[] { assert(!bridge::ShowInputMethod("test")); });
    WithHost("",[] {
        const auto start = bridge::Clock::now();
        assert(!bridge::HideInputMethod());
        assert(bridge::Clock::now()-start < std::chrono::milliseconds(1100));
    },900);
    assert(!bridge::SetClipboard(std::string(65537,'a').c_str()));
    bridge::Shutdown();
    std::puts("PASS: abstract socket, Unicode clipboard, authentication prefix, errors, EOF, deadlines and size limits");
}
