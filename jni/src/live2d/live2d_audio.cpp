#include "live2d/live2d_audio.h"

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#define LOG_TAG "AImGui_Live2D"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace aimgui {
namespace live2d {
namespace audio {

namespace {

struct Player {
    std::mutex               mtx;         // guards start/stop transitions
    std::vector<int16_t>     pcm;         // interleaved samples
    int                      channels = 1;
    int                      rate     = 44100;
    AAudioStream*            stream   = nullptr;
    std::thread              worker;
    std::atomic<size_t>      cursor{0};   // frames written so far
    std::atomic<size_t>      frames{0};
    std::atomic<bool>        playing{false};
    std::atomic<bool>        stopReq{false};

    void JoinWorker() {
        if (worker.joinable()) worker.join();
    }
};

Player g_p;

// Parse a little-endian 16-bit PCM WAV. Fills pcm/channels/rate.
bool ParseWav(const uint8_t* d, size_t n, std::vector<int16_t>& out,
              int& channels, int& rate) {
    if (n < 44 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0)
        return false;
    size_t p = 12;
    int fmt = 0, bits = 0, ch = 0, sr = 0;
    const uint8_t* data = nullptr; size_t dataLen = 0;
    auto rd16 = [](const uint8_t* q) { return (int)(q[0] | (q[1] << 8)); };
    auto rd32 = [](const uint8_t* q) {
        return (uint32_t)(q[0] | (q[1] << 8) | (q[2] << 16) | ((uint32_t)q[3] << 24));
    };
    while (p + 8 <= n) {
        const uint8_t* id = d + p;
        uint32_t sz = rd32(d + p + 4);
        const uint8_t* body = d + p + 8;
        if (p + 8 + sz > n) sz = (uint32_t)(n - p - 8);
        if (std::memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            fmt  = rd16(body);
            ch   = rd16(body + 2);
            sr   = (int)rd32(body + 4);
            bits = rd16(body + 14);
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = body; dataLen = sz;
        }
        p += 8 + sz + (sz & 1);  // chunks are word-aligned
    }
    if (fmt != 1 || bits != 16 || ch < 1 || sr < 8000 || !data || dataLen < 2) {
        LOGW("unsupported WAV (fmt=%d bits=%d ch=%d sr=%d)", fmt, bits, ch, sr);
        return false;
    }
    size_t samples = dataLen / 2;
    out.resize(samples);
    std::memcpy(out.data(), data, samples * 2);
    channels = ch; rate = sr;
    return true;
}

} // namespace

bool Playing() { return g_p.playing.load(); }

float Rms() {
    if (!g_p.playing.load()) return 0.0f;
    size_t c = g_p.cursor.load();
    size_t total = g_p.frames.load();
    if (total == 0 || c >= total) return 0.0f;
    int ch = g_p.channels;
    // A short window around the play cursor.
    size_t win = (size_t)(g_p.rate / 40);          // ~25 ms
    if (win < 64) win = 64;
    size_t start = c > win ? c - win : 0;
    size_t end = c;
    double acc = 0.0; size_t cnt = 0;
    for (size_t f = start; f < end; ++f) {
        int32_t s = g_p.pcm[f * ch];               // channel 0 is enough
        acc += (double)s * (double)s;
        ++cnt;
    }
    if (cnt == 0) return 0.0f;
    float rms = (float)std::sqrt(acc / (double)cnt) / 32768.0f;
    // Perceptual boost: speech RMS is low; scale + clamp for a lively mouth.
    float v = rms * 3.0f;
    return v > 1.0f ? 1.0f : v;
}

void Stop() {
    std::lock_guard<std::mutex> lk(g_p.mtx);
    g_p.stopReq.store(true);
    g_p.JoinWorker();
    if (g_p.stream) {
        AAudioStream_requestStop(g_p.stream);
        AAudioStream_close(g_p.stream);
        g_p.stream = nullptr;
    }
    g_p.playing.store(false);
    g_p.cursor.store(0);
}

bool PlayFile(const char* path) {
    // Read the whole file.
    std::FILE* fp = std::fopen(path, "rb");
    if (!fp) { LOGW("voice open failed: %s", path); return false; }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 44) { std::fclose(fp); return false; }
    std::vector<uint8_t> buf((size_t)sz);
    size_t rd = std::fread(buf.data(), 1, (size_t)sz, fp);
    std::fclose(fp);
    if (rd != (size_t)sz) return false;

    std::vector<int16_t> pcm; int ch = 0, rate = 0;
    if (!ParseWav(buf.data(), buf.size(), pcm, ch, rate)) return false;

    Stop();  // stop any current clip

    std::lock_guard<std::mutex> lk(g_p.mtx);
    g_p.pcm = std::move(pcm);
    g_p.channels = ch;
    g_p.rate = rate;
    g_p.frames.store(g_p.pcm.size() / (size_t)ch);
    g_p.cursor.store(0);

    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, ch);
    AAudioStreamBuilder_setSampleRate(b, rate);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_NONE);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_p.stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !g_p.stream) { LOGW("AAudio open failed (%d)", r); return false; }

    AAudioStream_requestStart(g_p.stream);
    g_p.stopReq.store(false);
    g_p.playing.store(true);

    g_p.worker = std::thread([]() {
        AAudioStream* st = g_p.stream;
        const int ch = g_p.channels;
        const size_t total = g_p.frames.load();
        size_t i = 0;
        const int block = 512;
        while (!g_p.stopReq.load() && i < total) {
            int n = (int)((total - i) < (size_t)block ? (total - i) : (size_t)block);
            aaudio_result_t w = AAudioStream_write(st, &g_p.pcm[i * ch], n,
                                                   200LL * 1000 * 1000); // 200ms
            if (w < 0) break;
            i += (w > 0 ? (size_t)w : (size_t)n);
            g_p.cursor.store(i);
        }
        g_p.playing.store(false);
    });
    return true;
}

} // namespace audio
} // namespace live2d
} // namespace aimgui
