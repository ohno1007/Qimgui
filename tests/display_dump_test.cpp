#include "platform/display_dump.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {
std::string Device(const std::string& id, const std::string& stack,
                   const std::string& rect = "Rect(0, 0 - 1216, 2640)",
                   const std::string& orientation = "0") {
    return "  DisplayDeviceInfo{\"Test\", uniqueId=\"" + id +
           "\", type VIRTUAL, state ON}\n"
           "    mAdapter=VirtualDisplayAdapter\n"
           "    mCurrentLayerStack=" + stack + "\n"
           "    mCurrentOrientation=" + orientation + "\n"
           "    mCurrentLayerStackRect=" + rect + "\n";
}
}

int main() {
    using android::detail::ParseDumpDisplayInfo;
    const auto physical = Device("local:1", "0");
    const auto recording = Device("virtual:recorder", "7", "Rect(0, 0 - 2640, 1216)", "1");
    auto parsed = ParseDumpDisplayInfo("Display Devices: size=2\n" + physical + recording);
    assert(parsed.size() == 2);
    assert(parsed[0].uniqueId == "local:1" && parsed[0].currentLayerStack == 0);
    assert(parsed[1].uniqueId == "virtual:recorder" && parsed[1].currentLayerStack == 7);
    assert(parsed[1].orientation == 1 && parsed[1].type == "VIRTUAL");
    assert(parsed[1].currentLayerStackRect.right == 2640 && parsed[1].currentLayerStackRect.bottom == 1216);

    // Cached/logical entries do not own live state. The old parser borrowed all
    // three values from the following device and assigned them to this ID.
    const std::string cached = "mDisplayDeviceInfo=DisplayDeviceInfo{\"cached\", uniqueId=\"stale\", type INTERNAL}\n";
    parsed = ParseDumpDisplayInfo(cached + recording);
    assert(parsed.size() == 1 && parsed[0].uniqueId == "virtual:recorder");
    const std::string incomplete = "DisplayDeviceInfo{\"missing rect\", uniqueId=\"bad\"}\n"
                                   "mCurrentLayerStack=8\nmCurrentOrientation=0\n";
    parsed = ParseDumpDisplayInfo(incomplete + recording);
    assert(parsed.size() == 1 && parsed[0].uniqueId == "virtual:recorder");

    // Android's dumpLocked ID is accepted if an OEM omits it from the summary.
    const std::string alternate = "DisplayDeviceInfo{\"secondary\", type VIRTUAL}\r\n"
                                  "\tmUniqueId = virtual:secondary\r\n"
                                  "\tmCurrentLayerStack = 12\r\n"
                                  "\tmCurrentOrientation = 3\r\n"
                                  "\tmCurrentLayerStackRect = Rect ( 0 , 0 - 720 , 1280 )\r\n";
    parsed = ParseDumpDisplayInfo(alternate);
    assert(parsed.size() == 1 && parsed[0].uniqueId == "virtual:secondary");
    assert(parsed[0].orientation == 3 && parsed[0].currentLayerStackRect.bottom == 1280);

    for (const auto& stack : {"-1", "4294967295", "4294967296", "bogus", "1 garbage", ""})
        assert(ParseDumpDisplayInfo(Device("invalid", stack)).empty());
    for (const auto& rect : {"null", "Rect(0, 0 - 0, 0)", "Rect(2, 1 - 1, 2)",
                            "Rect(0, 0 - 2147483648, 2640)", "Rect(-2147483648, 0 - 2147483647, 10)",
                            "Rect(0, 0 - 1080)", "Rect(0, 0 - 1080, 2400) garbage", ""})
        assert(ParseDumpDisplayInfo(Device("invalid", "1", rect)).empty());
    for (const auto& orientation : {"bad", "-1", "4", "4294967296"})
        assert(ParseDumpDisplayInfo(Device("invalid", "1", "Rect(0, 0 - 1, 1)", orientation)).empty());
    assert(ParseDumpDisplayInfo("not a display dump").empty());
    assert(ParseDumpDisplayInfo(Device("", "1")).empty());
    parsed = ParseDumpDisplayInfo(physical + recording + recording);
    assert(parsed.size() == 2);
    const auto legacy = android::detail::ParseDisplayInfo(recording);
    assert(legacy.size() == 1 && legacy[0].first == "virtual:recorder" && legacy[0].second == "7");
    std::puts("PASS: bounded display dump parsing, live physical/virtual state, malformed data and duplicate handling");
}
