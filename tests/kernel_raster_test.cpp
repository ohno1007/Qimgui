// Geometry, blending, clipping and validation checks for the kernel software
// rasteriser. Everything here is integer arithmetic, so the host results are
// bit-identical to what the module computes on the device.
#include "qk_raster.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr s32 kWidth = 64, kHeight = 48;

struct Target {
    std::vector<u32> storage;
    qk_surface surface{};

    Target(s32 width = kWidth, s32 height = kHeight) : storage((size_t)width * height, 0u) {
        surface.pixels = storage.data();
        surface.width = width;
        surface.height = height;
        surface.pitch = (u32)width * 4;
    }
    u32 At(s32 x, s32 y) const { return storage[(size_t)y * surface.width + x]; }
    u32 Alpha(s32 x, s32 y) const { return At(x, y) >> 24; }
    size_t Covered() const {
        size_t count = 0;
        for (u32 pixel : storage)
            if (pixel >> 24)
                ++count;
        return count;
    }
};

// Scene builder: positions in whole pixels, UVs in whole texels.
struct Scene {
    std::vector<qk_vertex> vertices;
    std::vector<u32> indices;
    std::vector<qk_command> commands;
    std::vector<qk_texture> textures{qk_texture{nullptr, 0, 0, 0, QK_TEXTURE_NONE}};

    u32 Vertex(float x, float y, u32 color, float u = 0, float v = 0) {
        vertices.push_back(qk_vertex{(s32)(x * QK_SUBPIXEL), (s32)(y * QK_SUBPIXEL),
                                     (s32)(u * QK_UV_ONE), (s32)(v * QK_UV_ONE), color});
        return (u32)vertices.size() - 1;
    }
    void Triangle(u32 a, u32 b, u32 c) {
        indices.insert(indices.end(), {a, b, c});
    }
    // Counter-clockwise in screen space, matching ImGui's winding. UVs are in
    // texels, like the rasteriser's own vertex format.
    void Quad(float x0, float y0, float x1, float y1, u32 color,
              float texels_x = 0, float texels_y = 0) {
        const u32 a = Vertex(x0, y0, color, 0.f, 0.f);
        const u32 b = Vertex(x1, y0, color, texels_x, 0.f);
        const u32 c = Vertex(x1, y1, color, texels_x, texels_y);
        const u32 d = Vertex(x0, y1, color, 0.f, texels_y);
        Triangle(a, b, c);
        Triangle(a, c, d);
    }
    void Command(qk_clip clip, u32 texture = 0) {
        const u32 offset = commands.empty() ? 0u : Consumed();
        commands.push_back(qk_command{offset, (u32)indices.size() - offset, texture, clip});
    }
    u32 Consumed() const {
        u32 used = 0;
        for (const qk_command& command : commands)
            used = command.index_offset + command.index_count > used
                       ? command.index_offset + command.index_count : used;
        return used;
    }
    qk_draw_data Data() const {
        return qk_draw_data{vertices.data(), (u32)vertices.size(), indices.data(),
                            (u32)indices.size(), commands.data(), (u32)commands.size(),
                            textures.data(), (u32)textures.size()};
    }
};

qk_clip Full() { return qk_clip{0, 0, kWidth, kHeight}; }

u32 Color(u32 r, u32 g, u32 b, u32 a) { return r | (g << 8) | (b << 16) | (a << 24); }

int Draw(Target& target, const Scene& scene, qk_raster_stats* stats = nullptr) {
    const qk_draw_data data = scene.Data();
    return qk_raster_draw(&target.surface, &data, stats);
}

void Fills() {
    Target target;
    qk_surface_fill(&target.surface, nullptr, 0xff123456u);
    assert(target.Covered() == (size_t)kWidth * kHeight);
    assert(target.At(0, 0) == 0xff123456u && target.At(kWidth - 1, kHeight - 1) == 0xff123456u);

    Target clipped;
    const qk_clip clip{4, 5, 10, 7};
    qk_surface_fill(&clipped.surface, &clip, 0xffffffffu);
    assert(clipped.Covered() == 6 * 2);
    assert(clipped.At(4, 5) == 0xffffffffu && clipped.At(9, 6) == 0xffffffffu);
    assert(clipped.At(3, 5) == 0 && clipped.At(10, 6) == 0 && clipped.At(4, 7) == 0);

    // Rects reaching past the surface are trimmed, not wrapped.
    Target spilling;
    const qk_clip outside{-20, -20, kWidth + 20, kHeight + 20};
    qk_surface_fill(&spilling.surface, &outside, 0xff00ff00u);
    assert(spilling.Covered() == (size_t)kWidth * kHeight);
}

void OpaqueQuad() {
    Target target;
    Scene scene;
    scene.Quad(8, 6, 24, 18, Color(200, 100, 50, 255));
    scene.Command(Full());
    qk_raster_stats stats{};
    assert(Draw(target, scene, &stats) == 0);
    assert(stats.triangles == 2 && stats.skipped == 0);
    // Pixel centres inside [8,24) x [6,18): exactly 16*12 of them.
    assert(stats.pixels == 16 * 12);
    assert(target.Covered() == 16 * 12);
    assert(target.At(8, 6) == 0xffc86432u);
    assert(target.At(23, 17) == 0xffc86432u);
    assert(target.At(7, 6) == 0 && target.At(24, 17) == 0 && target.At(8, 5) == 0);
    assert(stats.damage.x0 <= 8 && stats.damage.y0 <= 6);
    assert(stats.damage.x1 >= 24 && stats.damage.y1 >= 18);
}

// A quad split into two triangles must not blend its shared diagonal twice.
void SharedEdges() {
    Target target;
    Scene scene;
    scene.Quad(0, 0, kWidth, kHeight, Color(255, 0, 0, 128));
    scene.Command(Full());
    assert(Draw(target, scene) == 0);
    const u32 reference = target.At(1, 1);
    assert(reference >> 24 == 128);
    for (s32 y = 0; y < kHeight; y++)
        for (s32 x = 0; x < kWidth; x++)
            assert(target.At(x, y) == reference);
}

void Blending() {
    Target target;
    qk_surface_fill(&target.surface, nullptr, 0xffffffffu); // Opaque white, premultiplied.
    Scene scene;
    scene.Quad(0, 0, kWidth, kHeight, Color(255, 0, 0, 128));
    scene.Command(Full());
    assert(Draw(target, scene) == 0);
    const u32 pixel = target.At(10, 10);
    assert(pixel >> 24 == 255);                       // Opaque over opaque stays opaque.
    assert(((pixel >> 16) & 0xff) == 255);            // Red saturates.
    const u32 green = (pixel >> 8) & 0xff, blue = pixel & 0xff;
    assert(green >= 126 && green <= 128 && blue == green);

    // Alpha zero must leave the destination untouched.
    Target untouched;
    qk_surface_fill(&untouched.surface, nullptr, 0xff203040u);
    Scene invisible;
    invisible.Quad(0, 0, kWidth, kHeight, Color(255, 255, 255, 0));
    invisible.Command(Full());
    assert(Draw(untouched, invisible) == 0);
    assert(untouched.At(5, 5) == 0xff203040u);

    // Two 50% layers on transparent black: alpha accumulates, never exceeds 255.
    Target layered;
    Scene first, second;
    first.Quad(0, 0, kWidth, kHeight, Color(0, 0, 255, 128));
    first.Command(Full());
    second.Quad(0, 0, kWidth, kHeight, Color(0, 0, 255, 128));
    second.Command(Full());
    assert(Draw(layered, first) == 0 && Draw(layered, second) == 0);
    const u32 stacked = layered.At(3, 3) >> 24;
    assert(stacked >= 191 && stacked <= 193);
}

void Gradients() {
    Target target;
    Scene scene;
    // Opaque black on the left, opaque white on the right.
    const u32 a = scene.Vertex(0, 0, Color(0, 0, 0, 255));
    const u32 b = scene.Vertex(kWidth, 0, Color(255, 255, 255, 255));
    const u32 c = scene.Vertex(kWidth, kHeight, Color(255, 255, 255, 255));
    const u32 d = scene.Vertex(0, kHeight, Color(0, 0, 0, 255));
    scene.Triangle(a, b, c);
    scene.Triangle(a, c, d);
    scene.Command(Full());
    assert(Draw(target, scene) == 0);
    u32 previous = 0;
    for (s32 x = 0; x < kWidth; x++) {
        const u32 value = (target.At(x, kHeight / 2) >> 16) & 0xff;
        assert(value >= previous);
        previous = value;
        // Linear ramp sampled at pixel centres, within one quantisation step.
        const u32 want = (u32)((x + 0.5) * 255 / kWidth);
        assert(value + 2 >= want && value <= want + 2);
    }
    // The same ramp must appear on every row of the quad.
    for (s32 y = 0; y < kHeight; y++)
        assert(target.At(kWidth / 4, y) == target.At(kWidth / 4, 0));
}

void Clipping() {
    Target target;
    Scene scene;
    scene.Quad(0, 0, kWidth, kHeight, Color(255, 255, 255, 255));
    scene.Command(qk_clip{10, 10, 20, 20});
    assert(Draw(target, scene) == 0);
    assert(target.Covered() == 100);
    assert(target.Alpha(10, 10) == 255 && target.Alpha(19, 19) == 255);
    assert(target.Alpha(9, 10) == 0 && target.Alpha(20, 20) == 0);

    // An empty or inverted clip draws nothing and is not an error.
    Target empty;
    Scene degenerate;
    degenerate.Quad(0, 0, kWidth, kHeight, Color(255, 255, 255, 255));
    degenerate.Command(qk_clip{20, 20, 10, 10});
    assert(Draw(empty, degenerate) == 0 && empty.Covered() == 0);

    // Geometry far outside the surface is skipped without touching memory.
    Target offscreen;
    Scene far;
    far.Quad(-4000, -4000, -3000, -3000, Color(255, 0, 0, 255));
    far.Quad(3000, 3000, 4000, 4000, Color(255, 0, 0, 255));
    far.Command(Full());
    qk_raster_stats stats{};
    assert(Draw(offscreen, far, &stats) == 0);
    assert(offscreen.Covered() == 0 && stats.triangles == 0 && stats.skipped == 4);
    assert(stats.damage.x0 == stats.damage.x1);
}

void Textures() {
    // Alpha8: a 4x4 atlas whose right half is opaque.
    u8 alpha[16];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            alpha[y * 4 + x] = x >= 2 ? 255 : 0;
    Target target;
    Scene scene;
    scene.textures.push_back(qk_texture{alpha, 4, 4, 4, QK_TEXTURE_ALPHA8});
    scene.Quad(0, 0, 16, 16, Color(10, 20, 30, 255), 4.f, 4.f);
    scene.Command(qk_clip{0, 0, 16, 16}, 1);
    assert(Draw(target, scene) == 0);
    assert(target.Alpha(1, 1) == 0 && target.Alpha(14, 14) == 255);
    assert(target.At(14, 14) == 0xff0a141eu);
    assert(target.Covered() == 16 * 8);

    // RGBA32: colour comes from the texel, modulated by the vertex colour.
    u8 rgba[4 * 4];
    for (int i = 0; i < 4; i++) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 128;
        rgba[i * 4 + 2] = 64;
        rgba[i * 4 + 3] = 255;
    }
    Target colored;
    Scene textured;
    textured.textures.push_back(qk_texture{rgba, 2, 2, 8, QK_TEXTURE_RGBA32});
    textured.Quad(0, 0, 8, 8, Color(255, 255, 255, 255), 2.f, 2.f);
    textured.Command(qk_clip{0, 0, 8, 8}, 1);
    assert(Draw(colored, textured) == 0);
    assert(colored.At(4, 4) == 0xffff8040u);

    // Sampling clamps instead of wrapping: UVs beyond the atlas stay in range.
    Target clamped;
    Scene overflowing;
    overflowing.textures.push_back(qk_texture{rgba, 2, 2, 8, QK_TEXTURE_RGBA32});
    const u32 a = overflowing.Vertex(0, 0, Color(255, 255, 255, 255), -8.f, -8.f);
    const u32 b = overflowing.Vertex(8, 0, Color(255, 255, 255, 255), 16.f, -8.f);
    const u32 c = overflowing.Vertex(8, 8, Color(255, 255, 255, 255), 16.f, 16.f);
    overflowing.Triangle(a, b, c);
    overflowing.Command(qk_clip{0, 0, 8, 8}, 1);
    assert(Draw(clamped, overflowing) == 0);
    assert(clamped.At(6, 2) == 0xffff8040u);
}

void Degenerate() {
    Target target;
    Scene scene;
    const u32 a = scene.Vertex(4, 4, Color(255, 255, 255, 255));
    const u32 b = scene.Vertex(20, 4, Color(255, 255, 255, 255));
    scene.Triangle(a, b, b);           // Zero area.
    scene.Triangle(a, a, a);           // Single point.
    scene.Command(Full());
    qk_raster_stats stats{};
    assert(Draw(target, scene, &stats) == 0);
    assert(target.Covered() == 0 && stats.triangles == 0 && stats.skipped == 2);

    // Sub-pixel geometry may round away, but must never corrupt the surface.
    Target thin;
    Scene sliver;
    sliver.Quad(10.1f, 10.1f, 10.2f, 20.f, Color(255, 255, 255, 255));
    sliver.Command(Full());
    assert(Draw(thin, sliver) == 0);
    for (s32 y = 0; y < kHeight; y++)
        for (s32 x = 0; x < kWidth; x++)
            assert(x == 10 || thin.Alpha(x, y) == 0);
}

void Validation() {
    Target target;
    Scene scene;
    scene.Quad(0, 0, 8, 8, Color(255, 255, 255, 255));
    scene.Command(Full());

    qk_draw_data data = scene.Data();
    assert(qk_raster_draw(nullptr, &data, nullptr) == -22);
    assert(qk_raster_draw(&target.surface, nullptr, nullptr) == -22);

    qk_surface broken = target.surface;
    broken.pitch = 7;                                   // Not a multiple of four.
    assert(qk_raster_draw(&broken, &data, nullptr) == -22);
    broken = target.surface;
    broken.pitch = (u32)target.surface.width * 4 - 4;    // Too narrow for the row.
    assert(qk_raster_draw(&broken, &data, nullptr) == -22);
    broken = target.surface;
    broken.width = QK_MAX_DIMENSION + 1;
    assert(qk_raster_draw(&broken, &data, nullptr) == -22);

    // An index past the vertex array is rejected before any pixel is written.
    Scene bad_index = scene;
    bad_index.indices[0] = (u32)bad_index.vertices.size();
    data = bad_index.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);

    // So is a command whose range leaves the index array, or is not whole
    // triangles, or names a texture that does not exist.
    Scene bad_range = scene;
    bad_range.commands[0].index_count += 3;
    data = bad_range.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);
    Scene partial = scene;
    partial.commands[0].index_count -= 1;
    data = partial.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);
    Scene missing_texture = scene;
    missing_texture.commands[0].texture = 9;
    data = missing_texture.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);

    // Vertices beyond the coordinate budget are refused rather than overflowing.
    Scene huge = scene;
    huge.vertices[0].x = QK_MAX_DIMENSION * QK_SUBPIXEL + 1;
    data = huge.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);
    huge = scene;
    huge.vertices[0].u = QK_MAX_DIMENSION * QK_UV_ONE + 1;
    data = huge.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);

    // A texture description that does not match its pitch is refused.
    Scene bad_texture = scene;
    u8 pixels[4] = {0, 0, 0, 0};
    bad_texture.textures.push_back(qk_texture{pixels, 4, 1, 2, QK_TEXTURE_ALPHA8});
    data = bad_texture.Data();
    assert(qk_raster_draw(&target.surface, &data, nullptr) == -22);

    // Nothing above should have drawn anything.
    assert(target.Covered() == 0);

    // An empty scene is valid and clears the statistics.
    qk_raster_stats stats{};
    stats.triangles = 7;
    const qk_draw_data nothing{nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0};
    assert(qk_raster_draw(&target.surface, &nothing, &stats) == 0);
    assert(stats.triangles == 0 && stats.pixels == 0);
}

void LargeSurface() {
    // Panel-sized target: the window plane is at most this big on the device.
    Target target(1080, 2400);
    Scene scene;
    scene.Quad(0, 0, 1080, 2400, Color(16, 32, 48, 255));
    scene.Command(qk_clip{0, 0, 1080, 2400});
    qk_raster_stats stats{};
    assert(Draw(target, scene, &stats) == 0);
    assert(stats.pixels == 1080u * 2400u);
    assert(target.At(0, 0) == 0xff102030u && target.At(1079, 2399) == 0xff102030u);
    assert(stats.damage.x1 == 1080 && stats.damage.y1 == 2400);
}

} // namespace

int main() {
    Fills();
    OpaqueQuad();
    SharedEdges();
    Blending();
    Gradients();
    Clipping();
    Textures();
    Degenerate();
    Validation();
    LargeSurface();
    std::printf("kernel raster: all checks passed\n");
    return 0;
}
