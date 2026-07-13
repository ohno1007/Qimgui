#pragma once

// Shared contract between the Vulkan renderer (aimgui_core) and the Live2D
// Cubism glue (aimgui_live2d). Only meaningful when AIMGUI_LIVE2D is on, which
// in this project implies the pure-Vulkan Live2D path.
#ifdef AIMGUI_LIVE2D

#include "vulkan_wrapper.h" // real libvulkan prototypes in AIMGUI_REAL_VULKAN mode

namespace aimgui {

// Everything the Cubism Vulkan renderer needs, handed from renderer_vk to the
// Live2D layer once, before any model is loaded. The renderer owns all of
// these objects; the Live2D layer only borrows them.
struct Live2DVkContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          queue          = VK_NULL_HANDLE;
    uint32_t         queueFamily    = 0;
    VkCommandPool    commandPool    = VK_NULL_HANDLE; // dedicated pool for Cubism
    uint32_t         imageCount     = 2;
    VkExtent2D       extent{};                        // model render-target size
    VkFormat         colorFormat    = VK_FORMAT_UNDEFINED;
    VkFormat         depthFormat    = VK_FORMAT_UNDEFINED;
    VkImage          modelImage     = VK_NULL_HANDLE; // offscreen target (SAMPLED)
    VkImageView      modelView      = VK_NULL_HANDLE;
};

} // namespace aimgui

#endif // AIMGUI_LIVE2D
