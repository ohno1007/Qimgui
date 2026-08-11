#include "screen_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aimgui {
namespace {

// `screencap` with no -p writes a raw framebuffer: a small header of uint32s
// followed by tightly packed pixels. The header is 3 words (w, h, format) on
// most builds and 4 (plus colorspace) on others, and nothing in the output
// says which. Rather than key off the Android version, try both and keep the
// one whose dimensions actually account for the payload.
bool ParseRawHeader(const std::vector<uint8_t>& buf,
                    int* out_w, int* out_h, const uint8_t** out_px) {
    for (size_t hdr : {size_t{12}, size_t{16}}) {
        if (buf.size() < hdr) continue;
        uint32_t w = 0, h = 0, f = 0;
        std::memcpy(&w, buf.data() + 0, 4);
        std::memcpy(&h, buf.data() + 4, 4);
        std::memcpy(&f, buf.data() + 8, 4);
        if (w == 0 || h == 0 || w > 16384 || h > 16384) continue;
        if (f != 1) continue;                       // 1 = RGBA_8888
        if (buf.size() - hdr != (size_t)w * h * 4) continue;
        *out_w = (int)w; *out_h = (int)h; *out_px = buf.data() + hdr;
        return true;
    }
    return false;
}

bool RunScreencap(std::vector<uint8_t>* out) {
    FILE* pipe = ::popen("screencap", "r");
    if (!pipe) return false;
    out->clear();
    uint8_t chunk[64 * 1024];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        out->insert(out->end(), chunk, chunk + n);
        if (out->size() > 64u * 1024 * 1024) break;  // refuse to grow unbounded
    }
    ::pclose(pipe);
    return !out->empty();
}

// Average each source block down to one destination pixel. The downscale is
// what does most of the smoothing; the Gaussian afterwards just removes the
// blockiness a box filter leaves behind.
void BoxDownscale(const uint8_t* src, int sw, int sh,
                  uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        const int y0 = (int)((int64_t)y * sh / dh);
        const int y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * sh / dh));
        for (int x = 0; x < dw; ++x) {
            const int x0 = (int)((int64_t)x * sw / dw);
            const int x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * sw / dw));
            uint32_t acc[4] = {0, 0, 0, 0};
            uint32_t count  = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const uint8_t* row = src + ((size_t)sy * sw + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4) {
                    acc[0] += row[0]; acc[1] += row[1];
                    acc[2] += row[2]; acc[3] += row[3];
                    ++count;
                }
            }
            uint8_t* o = dst + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) o[c] = (uint8_t)(acc[c] / count);
        }
    }
}

std::vector<float> GaussianKernel(int radius) {
    const float sigma = std::max(1.0f, (float)radius * 0.5f);
    std::vector<float> k((size_t)radius * 2 + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = std::exp(-(float)(i * i) / (2.0f * sigma * sigma));
        k[(size_t)(i + radius)] = v;
        sum += v;
    }
    for (float& v : k) v /= sum;
    return k;
}

void BlurAxis(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst,
              int w, int h, bool horizontal, const std::vector<float>& k) {
    const int radius = (int)(k.size() / 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                int sx = std::clamp(horizontal ? x + i : x, 0, w - 1);
                int sy = std::clamp(horizontal ? y : y + i, 0, h - 1);
                const uint8_t* s  = &src[((size_t)sy * w + sx) * 4];
                const float    kw = k[(size_t)(i + radius)];
                for (int c = 0; c < 4; ++c) acc[c] += s[c] * kw;
            }
            uint8_t* o = &dst[((size_t)y * w + x) * 4];
            for (int c = 0; c < 4; ++c)
                o[c] = (uint8_t)std::clamp(acc[c] + 0.5f, 0.0f, 255.0f);
        }
    }
}

} // namespace

ScreenShot CaptureScreen(int target_long_side, int blur_radius) {
    ScreenShot out;

    std::vector<uint8_t> raw;
    if (!RunScreencap(&raw)) return out;

    int sw = 0, sh = 0;
    const uint8_t* px = nullptr;
    if (!ParseRawHeader(raw, &sw, &sh, &px)) return out;

    const int longer = std::max(sw, sh);
    const int target = std::max(16, target_long_side);
    if (longer <= 0) return out;

    const int dw = std::max(1, sw * target / longer);
    const int dh = std::max(1, sh * target / longer);

    std::vector<uint8_t> small((size_t)dw * dh * 4);
    BoxDownscale(px, sw, sh, small.data(), dw, dh);

    if (blur_radius > 0) {
        const std::vector<float> k = GaussianKernel(blur_radius);
        std::vector<uint8_t> tmp(small.size());
        BlurAxis(small, tmp,   dw, dh, true,  k);
        BlurAxis(tmp,   small, dw, dh, false, k);
    }

    // screencap reports the framebuffer as opaque; force alpha so the caller
    // controls the backdrop's transparency rather than inheriting stray values.
    for (size_t i = 3; i < small.size(); i += 4) small[i] = 255;

    out.width  = dw;
    out.height = dh;
    out.rgba   = std::move(small);
    return out;
}

} // namespace aimgui
