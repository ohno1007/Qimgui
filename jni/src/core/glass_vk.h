#pragma once

#include "glass.h"
#include "vulkan_wrapper.h"

namespace aimgui {

// Refracts the live screen through a set of panes, for the Vulkan backend.
// Compiled from jni/src/core/shaders/glass.{vert,frag} into glass_vk_spv.h;
// the OpenGL backend carries an ES translation of the same fragment shader.
class GlassVK {
public:
    // `colorFormat` and `renderPass` are the target being drawn into — the
    // scene pass when bloom is active, otherwise the swapchain's.
    bool Init(VkDevice device, VkDescriptorPool pool, VkRenderPass renderPass);
    void Shutdown();
    bool Ready() const { return m_Ready; }

    // Points the sampler at the newest mirrored frame. Cheap to call per frame
    // with an unchanged view; the descriptor is only rewritten when it moves.
    void SetScreenImage(VkImageView view);

    void Record(VkCommandBuffer cmd, int screenW, int screenH,
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

} // namespace aimgui
