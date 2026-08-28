#pragma once

#include "vulkan_wrapper.h"

#include <chrono>

namespace aimgui {

class BloomVK {
public:
    bool Init(VkDevice device,
              VkPhysicalDevice phys,
              VkDescriptorPool descPool,
              VkFormat colorFormat,
              uint32_t width,
              uint32_t height);

    void Shutdown();
    bool Ready() const { return m_Ready; }

    void SetIntensity(float i) { m_Intensity = i; }
    void SetSnapshotFrozen(bool frozen) { m_SnapshotFrozen = frozen; }

    void SetCompositeOverDest(bool over) { m_OverDest = over; }

    void SetModelBackground(VkImageView modelView);

    void RecordModelBackground(VkCommandBuffer cmd);

    VkRenderPass GetSceneRenderPass() const { return m_SceneRP; }

    bool BindToSwapchainRenderPass(VkRenderPass swapchainRP);

    void RegisterImGuiSnapshot();

    VkDescriptorSet GetSnapshotDescriptorSet() const { return m_PrevSceneImGuiDS; }

    void BeginScene(VkCommandBuffer cmd);

    // Breaks the scene pass open, copies what has been drawn so far into an
    // image the controls' pass can sample, and reopens the same framebuffer
    // with LOAD so nothing already drawn is lost.
    bool WidgetCaptureReady() const { return m_Ready && m_WidgetOk; }
    VkImageView WidgetCaptureView() const { return m_CaptureView; }
    void RecordWidgetCapture(VkCommandBuffer cmd);
    void EndSceneAndBlur(VkCommandBuffer cmd);
    void RecordCompositeDraw(VkCommandBuffer cmd);

    void RecordSnapshotCopy(VkCommandBuffer cmd);

private:
    bool SnapshotDue();

    bool             m_Ready  = false;
    VkDevice         m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_Phys   = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool   = VK_NULL_HANDLE;
    VkFormat         m_Format = VK_FORMAT_UNDEFINED;
    uint32_t         m_W = 0, m_H = 0;
    uint32_t         m_BW = 0, m_BH = 0;
    float            m_Intensity      = 0.75f;
    bool             m_SnapshotFrozen = false;
    bool             m_BlurInitialized = false;
    std::chrono::steady_clock::time_point m_LastSnapshot{};
    bool             m_OverDest       = false;

    VkRenderPass     m_SceneRP = VK_NULL_HANDLE;
    VkRenderPass     m_BlurRP  = VK_NULL_HANDLE;

    VkImage          m_CaptureImage = VK_NULL_HANDLE;
    VkImageView      m_CaptureView  = VK_NULL_HANDLE;
    VkDeviceMemory   m_CaptureMem   = VK_NULL_HANDLE;
    VkRenderPass     m_SceneLoadRP  = VK_NULL_HANDLE;
    bool             m_WidgetOk        = false;
    bool             m_CaptureFirstUse = true;

    VkImage          m_SceneImage = VK_NULL_HANDLE;
    VkImageView      m_SceneView  = VK_NULL_HANDLE;
    VkDeviceMemory   m_SceneMem   = VK_NULL_HANDLE;
    VkFramebuffer    m_SceneFB    = VK_NULL_HANDLE;

    VkImage          m_PrevSceneImage    = VK_NULL_HANDLE;
    VkImageView      m_PrevSceneView     = VK_NULL_HANDLE;
    VkDeviceMemory   m_PrevSceneMem      = VK_NULL_HANDLE;
    VkDescriptorSet  m_PrevSceneImGuiDS  = VK_NULL_HANDLE;
    bool             m_PrevSceneFirstUse = true;

    VkImage          m_BlurImage[2]{};
    VkImageView      m_BlurView[2]{};
    VkDeviceMemory   m_BlurMem[2]{};
    VkFramebuffer    m_BlurFB[2]{};

    VkSampler                m_Sampler          = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_DSL1             = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_DSL2             = VK_NULL_HANDLE;
    VkPipelineLayout         m_PLA              = VK_NULL_HANDLE;
    VkPipelineLayout         m_PLB              = VK_NULL_HANDLE;

    VkShaderModule           m_VS               = VK_NULL_HANDLE;
    VkShaderModule           m_FSThresh         = VK_NULL_HANDLE;
    VkShaderModule           m_FSBlur           = VK_NULL_HANDLE;
    VkShaderModule           m_FSComp           = VK_NULL_HANDLE;

    VkPipeline               m_PipeThresh       = VK_NULL_HANDLE;
    VkPipeline               m_PipeBlur         = VK_NULL_HANDLE;
    VkPipeline               m_PipeComp         = VK_NULL_HANDLE;
    VkPipeline               m_PipeCompOver     = VK_NULL_HANDLE;

    VkDescriptorSet          m_DSThresh         = VK_NULL_HANDLE;
    VkDescriptorSet          m_DSBlurH          = VK_NULL_HANDLE;
    VkDescriptorSet          m_DSBlurV          = VK_NULL_HANDLE;
    VkDescriptorSet          m_DSComp           = VK_NULL_HANDLE;
    VkDescriptorSet          m_DSModelBg        = VK_NULL_HANDLE;
};

}
