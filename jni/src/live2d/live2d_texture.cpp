#include "live2d/live2d_texture.h"

#include <android/imagedecoder.h>
#include <android/log.h>

#include <cstdint>
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

// Transition a single mip level of an image.
void BarrierLevel(VkCommandBuffer cb, VkImage img, uint32_t level,
                  VkImageLayout oldL, VkImageLayout newL,
                  VkAccessFlags src, VkAccessFlags dst,
                  VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL; b.newLayout = newL;
    b.srcAccessMask = src; b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1 };
    vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Upload a tightly-packed RGBA8 buffer into a sampled, mipmapped Vulkan image.
// Mipmaps matter: the model is drawn small (the floating "ball"), so 2K source
// textures must minify through a mip chain or they alias / look fuzzy.
bool Upload(const Live2DVkContext& ctx, const std::vector<unsigned char>& px,
            int w, int h, CubismImageVulkan& out) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(px.size());
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;

    uint32_t mip = 1;
    for (int m = (w > h ? w : h); m > 1; m >>= 1) ++mip;   // floor(log2(max))+1

    CubismBufferVulkan staging;
    staging.CreateBuffer(ctx.device, ctx.physicalDevice, size,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.Map(ctx.device, size);
    staging.MemCpy(px.data(), size);
    staging.UnMap(ctx.device);

    out.CreateImage(ctx.device, ctx.physicalDevice, w, h, static_cast<int>(mip), fmt,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT);
    VkImage img = out.GetImage();

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

    // All levels UNDEFINED → TRANSFER_DST, then copy the base level.
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mip, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    vkCmdCopyBufferToImage(cb, staging.GetBuffer(), img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Blit the chain, transitioning each finished source level to shader-read.
    int32_t mw = w, mh = h;
    for (uint32_t i = 1; i < mip; ++i) {
        BarrierLevel(cb, img, i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        int32_t nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
        blit.srcOffsets[1] = { mw, mh, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.dstOffsets[1] = { nw, nh, 1 };
        vkCmdBlitImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        BarrierLevel(cb, img, i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        mw = nw; mh = nh;
    }
    // Last level is still TRANSFER_DST → shader-read.
    BarrierLevel(cb, img, mip - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cb);
    staging.Destroy(ctx.device);

    out.SetCurrentLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    out.CreateView(ctx.device, fmt, VK_IMAGE_ASPECT_COLOR_BIT, static_cast<int>(mip));
    // Cubism 5 SDK r.2+ simplified CreateSampler to (device, maxAnisotropy,
    // mipLevel); address mode / filters are fixed internally (clamp + linear).
    out.CreateSampler(ctx.device, 1.0f, static_cast<csmUint32>(mip));
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
