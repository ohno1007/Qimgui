#pragma once

#ifdef AIMGUI_LIVE2D

#include "vulkan_wrapper.h"

namespace aimgui {

struct Live2DVkContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          queue          = VK_NULL_HANDLE;
    uint32_t         queueFamily    = 0;
    VkCommandPool    commandPool    = VK_NULL_HANDLE;
    uint32_t         imageCount     = 2;
    VkExtent2D       extent{};
    VkFormat         colorFormat    = VK_FORMAT_UNDEFINED;
    VkFormat         depthFormat    = VK_FORMAT_UNDEFINED;
    VkImage          modelImage     = VK_NULL_HANDLE;
    VkImageView      modelView      = VK_NULL_HANDLE;
};

}

#endif
