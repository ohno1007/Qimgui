#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace android::detail {

struct DumpDisplayInfo {
    std::string uniqueId;
    uint32_t currentLayerStack = 0;
    int32_t orientation = 0;
    std::string type;
    struct Rect {
        int32_t left = 0, top = 0, right = 0, bottom = 0;
    } currentLayerStackRect;
};

namespace display_dump {
inline bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
inline std::string_view Trim(std::string_view value) {
    while (!value.empty() && Space(value.front())) value.remove_prefix(1);
    while (!value.empty() && Space(value.back())) value.remove_suffix(1);
    return value;
}
inline std::string_view Assignment(std::string_view block, std::string_view key) {
    while (!block.empty()) {
        const auto end = block.find('\n');
        auto line = Trim(block.substr(0, end));
        if (line.substr(0, key.size()) == key) {
            line = Trim(line.substr(key.size()));
            if (!line.empty() && line.front() == '=') return Trim(line.substr(1));
        }
        if (end == std::string_view::npos) break;
        block.remove_prefix(end + 1);
    }
    return {};
}
template <typename Integer>
inline bool Number(std::string_view value, Integer& out) {
    value = Trim(value);
    if (value.empty()) return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), out);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
inline bool Coordinate(std::string_view& text, int32_t& out) {
    text = Trim(text);
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    if (result.ec != std::errc{}) return false;
    text.remove_prefix(static_cast<size_t>(result.ptr - text.data()));
    return true;
}
inline bool Consume(std::string_view& text, char expected) {
    text = Trim(text);
    if (text.empty() || text.front() != expected) return false;
    text.remove_prefix(1);
    return true;
}
inline bool Rectangle(std::string_view text, DumpDisplayInfo::Rect& rect) {
    text = Trim(text);
    if (text.substr(0, 4) == "Rect") text.remove_prefix(4);
    if (!Consume(text, '(') || !Coordinate(text, rect.left) || !Consume(text, ',') ||
        !Coordinate(text, rect.top) || !Consume(text, '-') ||
        !Coordinate(text, rect.right) || !Consume(text, ',') ||
        !Coordinate(text, rect.bottom) || !Consume(text, ')') || !Trim(text).empty())
        return false;
    const int64_t width = int64_t(rect.right) - rect.left;
    const int64_t height = int64_t(rect.bottom) - rect.top;
    return width > 0 && height > 0 && width <= std::numeric_limits<int32_t>::max() &&
           height <= std::numeric_limits<int32_t>::max();
}
inline std::string_view HeaderId(std::string_view header) {
    const auto key = header.find("uniqueId=");
    if (key == std::string_view::npos) return {};
    auto value = Trim(header.substr(key + 9));
    if (value.empty() || value.front() != '"') return {};
    value.remove_prefix(1);
    const auto end = value.find('"');
    return end == std::string_view::npos ? std::string_view{} : value.substr(0, end);
}
inline std::string_view HeaderType(std::string_view header) {
    const auto key = header.find("type ");
    if (key == std::string_view::npos) return {};
    auto value = Trim(header.substr(key + 5));
    const auto end = value.find_first_of(",}");
    return Trim(value.substr(0, end));
}
} // namespace display_dump

// DisplayDeviceInfo is also printed in cached/logical-display sections. Only
// accept entries with their own complete current device state; never search
// past the next entry for a missing field or throw on a changed dump format.
inline std::vector<DumpDisplayInfo> ParseDumpDisplayInfo(std::string_view dump) {
    constexpr std::string_view marker = "DisplayDeviceInfo";
    std::vector<DumpDisplayInfo> result;
    auto begin = dump.find(marker);
    while (begin != std::string_view::npos) {
        const auto next = dump.find(marker, begin + marker.size());
        const auto block = dump.substr(begin, next == std::string_view::npos ? next : next - begin);
        begin = next;
        const auto header = block.substr(0, block.find('\n'));
        auto id = display_dump::Assignment(block, "mUniqueId");
        if (id.empty()) id = display_dump::HeaderId(header);
        if (id.empty()) continue;

        DumpDisplayInfo info;
        if (!display_dump::Number(display_dump::Assignment(block, "mCurrentLayerStack"),
                                  info.currentLayerStack) ||
            info.currentLayerStack == std::numeric_limits<uint32_t>::max()) continue;
        const auto orientation = display_dump::Assignment(block, "mCurrentOrientation");
        if (!orientation.empty() && (!display_dump::Number(orientation, info.orientation) ||
                                    info.orientation < 0 || info.orientation > 3)) continue;
        if (!display_dump::Rectangle(display_dump::Assignment(block, "mCurrentLayerStackRect"),
                                     info.currentLayerStackRect)) continue;
        info.uniqueId.assign(id);
        info.type.assign(display_dump::HeaderType(header));
        bool duplicate = false;
        for (auto& existing : result) {
            if (existing.uniqueId == info.uniqueId && existing.currentLayerStack == info.currentLayerStack) {
                existing = info;
                duplicate = true;
                break;
            }
        }
        if (!duplicate) result.push_back(std::move(info));
    }
    return result;
}

inline std::vector<std::pair<std::string, std::string>> ParseDisplayInfo(std::string_view dump) {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& info : ParseDumpDisplayInfo(dump))
        result.emplace_back(info.uniqueId, std::to_string(info.currentLayerStack));
    return result;
}

} // namespace android::detail
