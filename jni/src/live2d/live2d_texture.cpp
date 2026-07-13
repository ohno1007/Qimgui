#include "live2d/live2d_texture.h"

#include <android/imagedecoder.h>
#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#define LOG_TAG "AImGui_Live2D"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace aimgui {
namespace live2d {

GLuint LoadTexture(const char* path) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        LOGW("texture open failed: %s", path);
        return 0;
    }

    AImageDecoder* decoder = nullptr;
    int result = AImageDecoder_createFromFd(fileno(fp), &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS || !decoder) {
        LOGW("AImageDecoder_createFromFd failed (%d): %s", result, path);
        std::fclose(fp);
        return 0;
    }

    // Force RGBA_8888, premultiplied alpha.
    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);
    AImageDecoder_setUnpremultipliedRequired(decoder, false);

    const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(decoder);
    const int width = AImageDecoderHeaderInfo_getWidth(info);
    const int height = AImageDecoderHeaderInfo_getHeight(info);
    const size_t stride = AImageDecoder_getMinimumStride(decoder);
    const size_t bufSize = stride * static_cast<size_t>(height);

    std::vector<unsigned char> pixels(bufSize);
    result = AImageDecoder_decodeImage(decoder, pixels.data(), stride, bufSize);
    AImageDecoder_delete(decoder);
    std::fclose(fp);

    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        LOGW("AImageDecoder_decodeImage failed (%d): %s", result, path);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace live2d
} // namespace aimgui
