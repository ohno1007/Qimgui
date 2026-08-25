#pragma once

#include "glass.h"
#include "vulkan_wrapper.h"

namespace aimgui {

class GlassVK {
public:

    bool Init(VkDevice device, VkDescriptorPool pool, VkRenderPass renderPass);
    void Shutdown();
    bool Ready() const { return m_Ready; }

    void SetScreenImage(VkImageView view);

    void Record(VkCommandBuffer cmd, int screenW, int screenH,
                int surfaceW, int surfaceH,
                const GlassRect* rects, int count);

private:
    bool             m_Ready  = false;
    VkDevice         m_Device = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool   = VK_NULL_HANDLE;

    VkSampler             m_Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DSL     = VK_NULL_HANDLE;
    VkDescriptorSet       m_DS      = VK_NULL_HANDLE;
    VkPipelineLayout      m_Layout  = VK_NULL_HANDLE;
    VkPipeline            m_Pipe    = VK_NULL_HANDLE;
    VkShaderModule        m_VS      = VK_NULL_HANDLE;
    VkShaderModule        m_FS      = VK_NULL_HANDLE;
    VkImageView           m_ScreenView = VK_NULL_HANDLE;
};

}
