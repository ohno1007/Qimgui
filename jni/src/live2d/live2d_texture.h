#pragma once

#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
#include "core/live2d_vk_bridge.h"

namespace aimgui {
namespace live2d {

using Live2D::Cubism::Framework::CubismImageVulkan;

bool LoadTextureVk(const Live2DVkContext& ctx, const char* path, CubismImageVulkan& out);

bool LoadTextureVkFromMemory(const Live2DVkContext& ctx, const void* data,
                             unsigned long size, CubismImageVulkan& out);

}
}
