// PNG → Vulkan texture loader using the NDK's AImageDecoder (API 30+). Live2D
// model textures are PNGs; this uploads them into a Cubism VK image wrapper.
#pragma once

#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
#include "core/live2d_vk_bridge.h"

namespace aimgui {
namespace live2d {

using Live2D::Cubism::Framework::CubismImageVulkan;

// Decode the PNG at `path` and upload it as a sampled RGBA Vulkan image.
// Returns true on success (filling `out`). Premultiplied alpha is applied
// (Cubism's default blend expects premultiplied textures).
bool LoadTextureVk(const Live2DVkContext& ctx, const char* path, CubismImageVulkan& out);

// Same, decoding a PNG already resident in memory (embedded model assets).
bool LoadTextureVkFromMemory(const Live2DVkContext& ctx, const void* data,
                             unsigned long size, CubismImageVulkan& out);

} // namespace live2d
} // namespace aimgui
