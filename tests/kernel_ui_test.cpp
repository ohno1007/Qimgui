// End-to-end check of the kernel UI path on the host: the MD3 gallery runs on
// the arena allocator and the libm subset, its draw lists are translated to
// integer draw data, and the software rasteriser turns them into premultiplied
// ARGB8888 pixels. The module runs this same code; only the surface it writes
// into and the floating-point guard around the frame differ.
#include "qk_arena.h"
#include "qk_raster.h"
#include "qk_ui.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr size_t kArenaBytes = 48u << 20;

struct Arena {
    void* block = nullptr;

    Arena() {
        block = std::aligned_alloc(16, kArenaBytes);
        assert(block);
        assert(qk_arena_init(block, kArenaBytes) == 0);
    }
    ~Arena() { std::free(block); }
};

struct Surface {
    std::vector<u32> pixels;
    qk_surface surface{};

    Surface(s32 width, s32 height) : pixels((size_t)width * height, 0u) {
        surface.pixels = pixels.data();
        surface.width = width;
        surface.height = height;
        surface.pitch = (u32)width * 4;
    }
    void Clear() { std::memset(pixels.data(), 0, pixels.size() * 4); }
    u32 At(s32 x, s32 y) const { return pixels[(size_t)y * surface.width + x]; }
    size_t Opaque() const {
        size_t count = 0;
        for (u32 pixel : pixels)
            if ((pixel >> 24) == 255)
                ++count;
        return count;
    }
    size_t Distinct() const {
        // Cheap proxy for "this is a UI, not a flat fill".
        std::vector<u32> seen;
        for (u32 pixel : pixels) {
            bool known = false;
            for (u32 value : seen)
                if (value == pixel) { known = true; break; }
            if (!known) {
                seen.push_back(pixel);
                if (seen.size() > 600)
                    break;
            }
        }
        return seen.size();
    }
    void WritePpm(const std::string& path) const {
        FILE* file = std::fopen(path.c_str(), "wb");
        assert(file);
        std::fprintf(file, "P6\n%d %d\n255\n", surface.width, surface.height);
        for (u32 pixel : pixels) {
            // Un-premultiply onto a mid grey so transparent regions read as
            // "Android content would show through here".
            const u32 alpha = pixel >> 24;
            const u32 background = 96;
            unsigned char rgb[3] = {
                (unsigned char)(((pixel >> 16) & 0xff) + background * (255 - alpha) / 255),
                (unsigned char)(((pixel >> 8) & 0xff) + background * (255 - alpha) / 255),
                (unsigned char)((pixel & 0xff) + background * (255 - alpha) / 255)};
            std::fwrite(rgb, 1, 3, file);
        }
        std::fclose(file);
    }
};

void Validate(const qk_draw_data& data, s32 width, s32 height) {
    assert(data.vertex_count > 0 && data.index_count > 0 && data.command_count > 0);
    assert(data.index_count % 3 == 0);
    bool any_texture = false, alpha8_or_rgba = false;
    for (u32 i = 0; i < data.command_count; i++) {
        const qk_command& command = data.commands[i];
        assert(command.index_count % 3 == 0);
        assert((u64)command.index_offset + command.index_count <= data.index_count);
        // Clip rects stay inside the window: the plane is exactly this size.
        assert(command.clip.x0 >= -1 && command.clip.y0 >= -1);
        assert(command.clip.x1 <= width + 1 && command.clip.y1 <= height + 1);
        if (command.texture) {
            any_texture = true;
            const qk_texture& texture = data.textures[command.texture];
            if (texture.format == QK_TEXTURE_ALPHA8 || texture.format == QK_TEXTURE_RGBA32)
                alpha8_or_rgba = true;
            assert(texture.pixels && texture.width > 0 && texture.height > 0);
        }
    }
    // The gallery draws text, so the font atlas must have reached the backend.
    assert(any_texture && alpha8_or_rgba);
    for (u32 i = 0; i < data.index_count; i++)
        assert(data.indices[i] < data.vertex_count);
}

struct Session {
    Surface target;
    s32 width, height;

    Session(s32 width, s32 height, bool dark, int scale_percent = 100)
        : target(width, height), width(width), height(height) {
        const qk_ui_config config{width, height, dark ? 1 : 0, scale_percent};
        assert(qk_ui_init(&config) == 0);
        // A second init on a live context is refused rather than leaking one.
        assert(qk_ui_init(&config) == -16);
    }
    ~Session() { qk_ui_shutdown(); }

    // Runs one frame and rasterises it. Returns the raster stats.
    qk_raster_stats Frame(const std::vector<qk_ui_event>& events = {},
                          u64 elapsed_us = 16667) {
        const qk_draw_data* data = nullptr;
        assert(qk_ui_frame(elapsed_us, events.data(), (u32)events.size(), &data) == 0);
        assert(data);
        Validate(*data, width, height);
        target.Clear();
        qk_raster_stats stats{};
        assert(qk_raster_draw(&target.surface, data, &stats) == 0);
        return stats;
    }
    void Settle(int frames = 90) {
        for (int i = 0; i < frames; i++)
            Frame();
    }
};

qk_ui_event Pointer(u32 type, s32 x, s32 y) {
    return qk_ui_event{type, x, y, 0, 0};
}

qk_ui_event Key(u32 code, bool down) {
    return qk_ui_event{QK_EVENT_KEY, 0, 0, code, down ? 1u : 0u};
}

void Rejections() {
    const qk_ui_config zero{0, 0, 0, 100};
    assert(qk_ui_init(&zero) == -22);
    assert(qk_ui_init(nullptr) == -22);
    const qk_ui_config huge{QK_MAX_DIMENSION + 1, 100, 0, 100};
    assert(qk_ui_init(&huge) == -22);
    // Calls before init are refused, not crashes.
    assert(qk_ui_resize(100, 100) == -19);
    const qk_draw_data* data = nullptr;
    assert(qk_ui_frame(16667, nullptr, 0, &data) == -19);
    qk_ui_shutdown();
}

void RendersGallery() {
    Session session(640, 640, false);
    session.Settle(4);
    const qk_raster_stats stats = session.Frame();

    assert(stats.triangles > 200);
    assert(stats.pixels > 640u * 640u / 4);
    // The window surface is filled: MD3 draws an opaque surface colour.
    assert(session.target.Opaque() > (size_t)640 * 640 * 3 / 4);
    // Text, outlines and state layers give a UI many distinct colours.
    assert(session.target.Distinct() > 40);
    assert(stats.damage.x1 - stats.damage.x0 > 400);
    assert(stats.damage.y1 - stats.damage.y0 > 400);

    struct qk_ui_stats ui{};
    qk_ui_get_stats(&ui);
    assert(ui.vertices > 400 && ui.commands > 0 && ui.textures >= 1);
    assert(ui.frames >= 5);
    std::printf("kernel ui: 640x640 %u triangles, %u vertices, %u commands, "
                "%u pixels, arena peak %u KiB\n",
                stats.triangles, ui.vertices, ui.commands, stats.pixels,
                ui.arena_peak / 1024);
    session.target.WritePpm("build/kernel-checks/gallery-light-640.ppm");
}

void PalettesDiffer() {
    u32 light_pixel = 0, dark_pixel = 0;
    {
        Session session(480, 800, false);
        session.Settle(4);
        session.Frame();
        light_pixel = session.target.At(240, 700);
        session.target.WritePpm("build/kernel-checks/gallery-light-480.ppm");
    }
    {
        Session session(480, 800, true);
        session.Settle(4);
        session.Frame();
        dark_pixel = session.target.At(240, 700);
        session.target.WritePpm("build/kernel-checks/gallery-dark-480.ppm");
    }
    assert((light_pixel >> 24) == 255 && (dark_pixel >> 24) == 255);
    // The MD3 surface colour is light in one palette and dark in the other.
    const u32 light_luma = ((light_pixel >> 16) & 0xff) + ((light_pixel >> 8) & 0xff) +
                           (light_pixel & 0xff);
    const u32 dark_luma = ((dark_pixel >> 16) & 0xff) + ((dark_pixel >> 8) & 0xff) +
                          (dark_pixel & 0xff);
    assert(light_luma > dark_luma + 150);
}

void HandlesInput() {
    Session session(640, 900, false);
    session.Settle(4);
    const size_t quiet = session.target.Opaque();

    // A press in the content area must reach the gallery: a pressed control
    // draws its state layer and ripple, so the frame changes.
    session.Frame({Pointer(QK_EVENT_POINTER_DOWN, 320, 420)});
    const qk_raster_stats pressed = session.Frame({Pointer(QK_EVENT_POINTER_MOVE, 320, 420)});
    assert(pressed.triangles > 200);
    session.Frame({Pointer(QK_EVENT_POINTER_UP, 320, 420)});
    assert(session.target.Opaque() > quiet / 2);

    // Keyboard navigation and text keys exercise the Linux keycode mapping.
    for (u32 code : {15u /* Tab */, 15u, 103u /* Up */, 108u /* Down */, 28u /* Enter */}) {
        session.Frame({Key(code, true)});
        session.Frame({Key(code, false)});
    }
    // Shifted and plain letters, digits and punctuation.
    session.Frame({Key(42u, true), Key(30u, true), Key(30u, false), Key(42u, false)});
    session.Frame({Key(2u, true), Key(2u, false), Key(11u, true), Key(11u, false)});
    session.Frame({Key(53u, true), Key(53u, false), Key(57u, true), Key(57u, false)});
    const qk_raster_stats after = session.Frame();
    assert(after.triangles > 200);

    // Scroll events are accepted; the gallery may or may not have a scrollbar.
    session.Frame({qk_ui_event{QK_EVENT_SCROLL, 0, -3, 0, 0}});

    // Events outside the window are the input layer's business, not ours: the
    // UI must still produce a valid frame.
    session.Frame({Pointer(QK_EVENT_POINTER_DOWN, -50, -50),
                   Pointer(QK_EVENT_POINTER_UP, 5000, 5000)});
    // A malformed batch is rejected without running a frame.
    assert(qk_ui_frame(16667, nullptr, 3, nullptr) == -22);
}

void Resizes() {
    Session session(400, 400, false);
    session.Settle(4);
    assert(qk_ui_resize(0, 100) == -22);
    assert(qk_ui_resize(QK_MAX_DIMENSION + 1, 100) == -22);

    // Rotation-like changes: the window keeps rendering at every size.
    const s32 sizes[][2] = {{1080, 600}, {600, 1080}, {240, 240}, {1080, 2400}};
    for (const auto& size : sizes) {
        Surface target(size[0], size[1]);
        assert(qk_ui_resize(size[0], size[1]) == 0);
        const qk_draw_data* data = nullptr;
        for (int frame = 0; frame < 3; frame++)
            assert(qk_ui_frame(16667, nullptr, 0, &data) == 0);
        Validate(*data, size[0], size[1]);
        qk_raster_stats stats{};
        assert(qk_raster_draw(&target.surface, data, &stats) == 0);
        assert(stats.triangles > 50);
        assert(target.Opaque() > (size_t)size[0] * size[1] / 2);
        if (size[0] == 1080 && size[1] == 2400)
            target.WritePpm("build/kernel-checks/gallery-panel-1080x2400.ppm");
    }
}

// ImGui dereferences whatever its allocator returns: there is no way to make
// it tolerate a failed allocation. The port's answer is a region with measured
// headroom, refused outright when it is too small. This pins both halves of
// that contract -- the refusal, and the headroom on the largest window.
void MinimumArena() {
    void* small = std::aligned_alloc(16, 1u << 20);
    assert(small);
    assert(qk_arena_init(small, 1u << 20) == 0);
    const qk_ui_config config{640, 640, 0, 100};
    assert(qk_ui_init(&config) == -12);
    std::free(small);

    void* exact = std::aligned_alloc(16, QK_ARENA_MINIMUM);
    assert(exact);
    assert(qk_arena_init(exact, QK_ARENA_MINIMUM) == 0);
    {
        // The largest window the plane can carry on this panel.
        Session session(1080, 2400, true);
        session.Settle(8);
        for (int frame = 0; frame < 12; frame++)
            session.Frame({Pointer(QK_EVENT_POINTER_DOWN, 540, 1200),
                           Pointer(QK_EVENT_POINTER_UP, 540, 1200)});
        struct qk_arena_stats stats{};
        qk_arena_get_stats(&stats);
        assert(stats.overflows == 0 && !qk_arena_exhausted());
        // Keep a wide margin: a future page must not quietly eat the reserve.
        assert(stats.peak * 2 < stats.capacity);
        std::printf("kernel ui: minimum arena %u KiB, peak %u KiB (%u%%)\n",
                    stats.capacity / 1024, stats.peak / 1024,
                    (unsigned)((u64)stats.peak * 100 / stats.capacity));
    }
    assert(qk_arena_check() == 0);
    std::free(exact);

    static Arena restored;
    assert(!qk_arena_exhausted() && qk_arena_check() == 0);
}

void Throughput() {
    Session session(1080, 2400, true);
    session.Settle(4);
    const int frames = 20;
    const auto start = std::chrono::steady_clock::now();
    qk_raster_stats stats{};
    for (int frame = 0; frame < frames; frame++)
        stats = session.Frame();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double ms =
        std::chrono::duration<double, std::milli>(elapsed).count() / frames;
    // Informational: the device is slower than the host, and this build carries
    // sanitisers. It is here to catch an order-of-magnitude regression.
    std::printf("kernel ui: 1080x2400 %.1f ms/frame instrumented, %u triangles, "
                "%u pixels\n", ms, stats.triangles, stats.pixels);
    assert(stats.pixels > 1080u * 2400u / 4);
}

} // namespace

int main() {
    static Arena arena;
    Rejections();
    RendersGallery();
    PalettesDiffer();
    HandlesInput();
    Resizes();
    MinimumArena();
    Throughput();
    assert(qk_arena_check() == 0);
    struct qk_arena_stats stats{};
    qk_arena_get_stats(&stats);
    std::printf("kernel ui: arena %u KiB capacity, peak %u KiB, %llu allocations, "
                "%llu failures\n", stats.capacity / 1024, stats.peak / 1024,
                (unsigned long long)stats.allocations,
                (unsigned long long)stats.failures);
    std::printf("kernel ui: all checks passed\n");
    return 0;
}
