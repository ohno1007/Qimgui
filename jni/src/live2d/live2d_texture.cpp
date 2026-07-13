#include "live2d/live2d_texture.h"

#include <android/imagedecoder.h>
#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <vector>

#define LOG_TAG "AImGui_Live2D"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using namespace Live2D::Cubism::Framework;

namespace aimgui {
namespace live2d {

namespace {

// Decode an AImageDecoder into a tightly-packed RGBA8 buffer.
bool DecodeRGBA(AImageDecoder* decoder, std::vector<unsigned char>& out, int& w, int& h) {
    AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGBA_8888);
    // Premultiplied alpha (Cubism uses IsPremultipliedAlpha(true)).
    AImageDecoder_setUnpremultipliedRequired(decoder, false);

    const AImageDecoderHeaderInfo* info = AImageDecoder_getHeaderInfo(decoder);
    w = AImageDecoderHeaderInfo_getWidth(info);
    h = AImageDecoderHeaderInfo_getHeight(info);
    const size_t stride = AImageDecoder_getMinimumStride(decoder);

    std::vector<unsigned char> raw(stride * static_cast<size_t>(h));
    int r = AImageDecoder_decodeImage(decoder, raw.data(), stride, raw.size());
    if (r != ANDROID_IMAGE_DECODER_SUCCESS) {
        LOGW("AImageDecoder_decodeImage failed (%d)", r);
        return false;
    }

    // Repack to a tight w*4 stride for vkCmdCopyBufferToImage.
    const size_t tight = static_cast<size_t>(w) * 4;
    out.resize(tight * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y)
        std::memcpy(out.data() + tight * y, raw.data() + stride * y, tight);
    return true;
}

// Upload a tightly-packed RGBA8 buffer into a sampled Vulkan image.
bool Upload(const Live2DVkContext& ctx, const std::vector<unsigned char>& px,
            int w, int h, CubismImageVulkan& out) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(px.size());
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;

    CubismBufferVulkan staging;
    staging.CreateBuffer(ctx.device, ctx.physicalDevice, size,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Map(ctx.device, size);
    staging.MemCpy(px.data(), size);
    staging.UnMap(ctx.device);

    out.CreateImage(ctx.device, ctx.physicalDevice, w, h, /*mipLevel=*/1, fmt,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // One-time command: transfer-dst → copy → shader-read.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = ctx.commandPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(ctx.device, &cai, &cb) != VK_SUCCESS) {
        staging.Destroy(ctx.device);
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    out.SetImageLayout(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    vkCmdCopyBufferToImage(cb, staging.GetBuffer(), out.GetImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    out.SetImageLayout(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cb);
    staging.Destroy(ctx.device);

    out.CreateView(ctx.device, fmt, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    out.CreateSampler(ctx.device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                      VK_FILTER_LINEAR, VK_FILTER_LINEAR,
                      VK_SAMPLER_MIPMAP_MODE_LINEAR, 1.0f, 1);
    return true;
}

} // namespace

bool LoadTextureVkFromMemory(const Live2DVkContext& ctx, const void* data,
                             unsigned long size, CubismImageVulkan& out) {
    AImageDecoder* decoder = nullptr;
    int r = AImageDecoder_createFromBuffer(data, size, &decoder);
    if (r != ANDROID_IMAGE_DECODER_SUCCESS || !decoder) {
        LOGW("AImageDecoder_createFromBuffer failed (%d)", r);
        return false;
    }
    std::vector<unsigned char> px; int w = 0, h = 0;
    bool ok = DecodeRGBA(decoder, px, w, h);
    AImageDecoder_delete(decoder);
    return ok && Upload(ctx, px, w, h, out);
}

bool LoadTextureVk(const Live2DVkContext& ctx, const char* path, CubismImageVulkan& out) {
    FILE* fp = std::fopen(path, "rb");
    if (!fp) { LOGW("texture open failed: %s", path); return false; }
    AImageDecoder* decoder = nullptr;
    int r = AImageDecoder_createFromFd(fileno(fp), &decoder);
    if (r != ANDROID_IMAGE_DECODER_SUCCESS || !decoder) {
        LOGW("AImageDecoder_createFromFd failed (%d): %s", r, path);
        std::fclose(fp);
        return false;
    }
    std::vector<unsigned char> px; int w = 0, h = 0;
    bool ok = DecodeRGBA(decoder, px, w, h);
    AImageDecoder_delete(decoder);
    std::fclose(fp);
    return ok && Upload(ctx, px, w, h, out);
}

} // namespace live2d
} // namespace aimgui
