#include "bloom_vk.h"

#include "bloom_vk_spv.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <android/log.h>
#include <cstring>

#define LOG_TAG "AImGui_BloomVK"
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

namespace aimgui {

namespace {

struct PushConsts {
    float dir_x;
    float dir_y;
    float intensity;
    float _pad;
};

uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

bool CreateImage2D(VkDevice device, VkPhysicalDevice phys, VkFormat fmt,
                   uint32_t w, uint32_t h, VkImage* out_image,
                   VkImageView* out_view, VkDeviceMemory* out_mem) {
    VkImageCreateInfo ic{};
    ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = fmt;
    ic.extent = { w, h, 1 };
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ic, nullptr, out_image) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, *out_image, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = FindMemoryType(phys, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(device, &ai, nullptr, out_mem) != VK_SUCCESS) return false;
    if (vkBindImageMemory(device, *out_image, *out_mem, 0) != VK_SUCCESS) return false;

    VkImageViewCreateInfo vc{};
    vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vc.image = *out_image;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = fmt;
    vc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &vc, nullptr, out_view) != VK_SUCCESS) return false;

    return true;
}

bool CreateColorRenderPass(VkDevice device, VkFormat fmt, VkAttachmentLoadOp loadOp,
                           VkRenderPass* out_rp) {
    VkAttachmentDescription att{};
    att.format = fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = loadOp;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2;
    rpi.pDependencies = deps;

    return vkCreateRenderPass(device, &rpi, nullptr, out_rp) == VK_SUCCESS;
}

bool CreateFramebuffer(VkDevice device, VkRenderPass rp, VkImageView view,
                       uint32_t w, uint32_t h, VkFramebuffer* out_fb) {
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = rp;
    fi.attachmentCount = 1;
    fi.pAttachments = &view;
    fi.width = w;
    fi.height = h;
    fi.layers = 1;
    return vkCreateFramebuffer(device, &fi, nullptr, out_fb) == VK_SUCCESS;
}

VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    si.codeSize = bytes;
    si.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &si, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

bool CreateFullscreenPipeline(VkDevice device, VkRenderPass rp, VkPipelineLayout layout,
                              VkShaderModule vs, VkShaderModule fs, VkPipeline* out,
                              bool blend = false) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend) {

        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState   = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &ds;
    pi.pColorBlendState    = &cb;
    pi.pDynamicState       = &dyn;
    pi.layout              = layout;
    pi.renderPass          = rp;
    pi.subpass             = 0;
    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, out) == VK_SUCCESS;
}

void WriteSampledImage(VkDevice device, VkDescriptorSet set, uint32_t binding,
                       VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo ii{};
    ii.sampler     = sampler;
    ii.imageView   = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = binding;
    w.dstArrayElement = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

void SetFullViewport(VkCommandBuffer cmd, uint32_t w, uint32_t h) {
    VkViewport vp{ 0, 0, (float)w, (float)h, 0.0f, 1.0f };
    VkRect2D sc{ {0, 0}, { w, h } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

}

bool BloomVK::Init(VkDevice device, VkPhysicalDevice phys, VkDescriptorPool pool,
                   VkFormat fmt, uint32_t w, uint32_t h) {
    m_Device = device;
    m_Phys   = phys;
    m_Pool   = pool;
    m_Format = fmt;
    m_W = w; m_H = h;
    m_BW = w / 2; m_BH = h / 2;
    if (m_BW < 16 || m_BH < 16) return false;

    if (!CreateColorRenderPass(device, fmt, VK_ATTACHMENT_LOAD_OP_CLEAR, &m_SceneRP)) {
        LOGE("scene render pass"); Shutdown(); return false;
    }
    if (!CreateColorRenderPass(device, fmt, VK_ATTACHMENT_LOAD_OP_DONT_CARE, &m_BlurRP)) {
        LOGE("blur render pass"); Shutdown(); return false;
    }

    if (!CreateImage2D(device, phys, fmt, m_W, m_H, &m_SceneImage, &m_SceneView, &m_SceneMem) ||
        !CreateFramebuffer(device, m_SceneRP, m_SceneView, m_W, m_H, &m_SceneFB)) {
        LOGE("scene image/FB"); Shutdown(); return false;
    }
    if (!CreateImage2D(device, phys, fmt, m_W, m_H,
                       &m_PrevSceneImage, &m_PrevSceneView, &m_PrevSceneMem)) {
        LOGE("prev-scene image"); Shutdown(); return false;
    }
    m_PrevSceneFirstUse = true;
    for (int i = 0; i < 2; ++i) {
        if (!CreateImage2D(device, phys, fmt, m_BW, m_BH,
                           &m_BlurImage[i], &m_BlurView[i], &m_BlurMem[i]) ||
            !CreateFramebuffer(device, m_BlurRP, m_BlurView[i], m_BW, m_BH, &m_BlurFB[i])) {
            LOGE("blur image/FB %d", i); Shutdown(); return false;
        }
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(device, &si, nullptr, &m_Sampler) != VK_SUCCESS) {
        LOGE("sampler"); Shutdown(); return false;
    }

    VkDescriptorSetLayoutBinding b1{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                     1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo dli{};
    dli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dli.bindingCount = 1;
    dli.pBindings = &b1;
    if (vkCreateDescriptorSetLayout(device, &dli, nullptr, &m_DSL1) != VK_SUCCESS) {
        LOGE("DSL1"); Shutdown(); return false;
    }

    VkDescriptorSetLayoutBinding b2[2]{
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    dli.bindingCount = 2;
    dli.pBindings = b2;
    if (vkCreateDescriptorSetLayout(device, &dli, nullptr, &m_DSL2) != VK_SUCCESS) {
        LOGE("DSL2"); Shutdown(); return false;
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(PushConsts);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_DSL1;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_PLA) != VK_SUCCESS) {
        LOGE("PLA"); Shutdown(); return false;
    }
    plci.pSetLayouts = &m_DSL2;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_PLB) != VK_SUCCESS) {
        LOGE("PLB"); Shutdown(); return false;
    }

    m_VS       = CreateShaderModule(device, bloom_vk_spv::kVS,           sizeof(bloom_vk_spv::kVS));
    m_FSThresh = CreateShaderModule(device, bloom_vk_spv::kFS_Threshold, sizeof(bloom_vk_spv::kFS_Threshold));
    m_FSBlur   = CreateShaderModule(device, bloom_vk_spv::kFS_Blur,      sizeof(bloom_vk_spv::kFS_Blur));
    m_FSComp   = CreateShaderModule(device, bloom_vk_spv::kFS_Composite, sizeof(bloom_vk_spv::kFS_Composite));
    if (!m_VS || !m_FSThresh || !m_FSBlur || !m_FSComp) {
        LOGE("shader modules"); Shutdown(); return false;
    }

    if (!CreateFullscreenPipeline(device, m_BlurRP, m_PLA, m_VS, m_FSThresh, &m_PipeThresh) ||
        !CreateFullscreenPipeline(device, m_BlurRP, m_PLA, m_VS, m_FSBlur,   &m_PipeBlur)) {
        LOGE("blur pipelines"); Shutdown(); return false;
    }

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &m_DSL1;
    if (vkAllocateDescriptorSets(device, &dai, &m_DSThresh) != VK_SUCCESS ||
        vkAllocateDescriptorSets(device, &dai, &m_DSBlurH) != VK_SUCCESS ||
        vkAllocateDescriptorSets(device, &dai, &m_DSBlurV) != VK_SUCCESS) {
        LOGE("alloc DS"); Shutdown(); return false;
    }
    dai.pSetLayouts = &m_DSL2;
    if (vkAllocateDescriptorSets(device, &dai, &m_DSComp) != VK_SUCCESS ||
        vkAllocateDescriptorSets(device, &dai, &m_DSModelBg) != VK_SUCCESS) {
        LOGE("alloc DS comp"); Shutdown(); return false;
    }

    WriteSampledImage(device, m_DSThresh, 0, m_SceneView,  m_Sampler);
    WriteSampledImage(device, m_DSBlurH,  0, m_BlurView[0], m_Sampler);
    WriteSampledImage(device, m_DSBlurV,  0, m_BlurView[1], m_Sampler);
    WriteSampledImage(device, m_DSComp,   0, m_SceneView,   m_Sampler);
    WriteSampledImage(device, m_DSComp,   1, m_BlurView[0], m_Sampler);

    m_Ready = true;
    return true;
}

void BloomVK::RegisterImGuiSnapshot() {
    if (!m_Ready || m_PrevSceneView == VK_NULL_HANDLE) return;

    m_PrevSceneImGuiDS = ImGui_ImplVulkan_AddTexture(
        m_PrevSceneView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

bool BloomVK::BindToSwapchainRenderPass(VkRenderPass swapchainRP) {
    if (!m_Ready) return false;
    if (m_PipeComp) {
        vkDestroyPipeline(m_Device, m_PipeComp, nullptr);
        m_PipeComp = VK_NULL_HANDLE;
    }
    if (m_PipeCompOver) {
        vkDestroyPipeline(m_Device, m_PipeCompOver, nullptr);
        m_PipeCompOver = VK_NULL_HANDLE;
    }
    if (!CreateFullscreenPipeline(m_Device, swapchainRP, m_PLB, m_VS, m_FSComp, &m_PipeComp)) {
        LOGE("composite pipeline"); return false;
    }

    if (!CreateFullscreenPipeline(m_Device, swapchainRP, m_PLB, m_VS, m_FSComp,
                                  &m_PipeCompOver, true)) {
        LOGE("composite-over pipeline"); return false;
    }
    return true;
}

void BloomVK::SetModelBackground(VkImageView modelView) {
    if (!m_Ready || m_DSModelBg == VK_NULL_HANDLE) return;
    if (modelView == VK_NULL_HANDLE) return;

    WriteSampledImage(m_Device, m_DSModelBg, 0, modelView, m_Sampler);
    WriteSampledImage(m_Device, m_DSModelBg, 1, modelView, m_Sampler);
}

void BloomVK::RecordModelBackground(VkCommandBuffer cmd) {
    if (!m_Ready || m_PipeComp == VK_NULL_HANDLE || m_DSModelBg == VK_NULL_HANDLE) return;
    SetFullViewport(cmd, m_W, m_H);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipeComp);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PLB, 0, 1, &m_DSModelBg, 0, nullptr);
    PushConsts pc{};
    pc.intensity = 0.0f;
    vkCmdPushConstants(cmd, m_PLB, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void BloomVK::BeginScene(VkCommandBuffer cmd) {
    if (!m_Ready) return;
    VkClearValue clear{};
    clear.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = m_SceneRP;
    rpi.framebuffer = m_SceneFB;
    rpi.renderArea.extent = { m_W, m_H };
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
    SetFullViewport(cmd, m_W, m_H);
}

void BloomVK::EndSceneAndBlur(VkCommandBuffer cmd) {
    if (!m_Ready) return;
    vkCmdEndRenderPass(cmd);

    auto blur_pass = [&](VkFramebuffer fb, VkPipeline pipe, VkDescriptorSet set,
                         const PushConsts& pc) {
        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = m_BlurRP;
        rpi.framebuffer = fb;
        rpi.renderArea.extent = { m_BW, m_BH };
        rpi.clearValueCount = 0;
        vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        SetFullViewport(cmd, m_BW, m_BH);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PLA, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_PLA, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    };

    if (m_Intensity <= 0.001f && m_BlurInitialized) return;

    PushConsts pc{};

    blur_pass(m_BlurFB[0], m_PipeThresh, m_DSThresh, pc);

    for (int iter = 0; iter < 2; ++iter) {
        pc.dir_x = 1.0f / (float)m_BW;
        pc.dir_y = 0.0f;
        blur_pass(m_BlurFB[1], m_PipeBlur, m_DSBlurH, pc);

        pc.dir_x = 0.0f;
        pc.dir_y = 1.0f / (float)m_BH;
        blur_pass(m_BlurFB[0], m_PipeBlur, m_DSBlurV, pc);
    }
    m_BlurInitialized = true;
}

void BloomVK::RecordCompositeDraw(VkCommandBuffer cmd) {
    VkPipeline pipe = m_OverDest ? m_PipeCompOver : m_PipeComp;
    if (!m_Ready || pipe == VK_NULL_HANDLE) return;
    SetFullViewport(cmd, m_W, m_H);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PLB, 0, 1, &m_DSComp, 0, nullptr);
    PushConsts pc{};
    pc.intensity = m_Intensity;
    vkCmdPushConstants(cmd, m_PLB, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool BloomVK::SnapshotDue() {
    const auto now = std::chrono::steady_clock::now();
    if (now - m_LastSnapshot < std::chrono::milliseconds(200)) return false;
    m_LastSnapshot = now;
    return true;
}

void BloomVK::RecordSnapshotCopy(VkCommandBuffer cmd) {
    if (!m_Ready || m_PrevSceneImage == VK_NULL_HANDLE) return;
    if (m_SnapshotFrozen) return;

    if (!SnapshotDue()) return;

    VkImageMemoryBarrier b[2]{};
    b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcQueueFamilyIndex = b[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[0].image = m_SceneImage;
    b[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    b[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b[1].srcAccessMask = m_PrevSceneFirstUse ? 0 : VK_ACCESS_SHADER_READ_BIT;
    b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].oldLayout = m_PrevSceneFirstUse ? VK_IMAGE_LAYOUT_UNDEFINED
                                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].srcQueueFamilyIndex = b[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b[1].image = m_PrevSceneImage;
    b[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 2, b);

    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { m_W, m_H, 1 };
    vkCmdCopyImage(cmd,
        m_SceneImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        m_PrevSceneImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 2, b);

    m_PrevSceneFirstUse = false;
}

void BloomVK::Shutdown() {
    if (m_Device == VK_NULL_HANDLE) return;

    VkDescriptorSet sets[5]{ m_DSThresh, m_DSBlurH, m_DSBlurV, m_DSComp, m_DSModelBg };
    uint32_t n = 0;
    for (auto s : sets) if (s != VK_NULL_HANDLE) sets[n++] = s;
    if (n > 0 && m_Pool != VK_NULL_HANDLE)
        vkFreeDescriptorSets(m_Device, m_Pool, n, sets);

    if (m_PipeThresh)   vkDestroyPipeline(m_Device, m_PipeThresh, nullptr);
    if (m_PipeBlur)     vkDestroyPipeline(m_Device, m_PipeBlur,   nullptr);
    if (m_PipeComp)     vkDestroyPipeline(m_Device, m_PipeComp,   nullptr);
    if (m_PipeCompOver) vkDestroyPipeline(m_Device, m_PipeCompOver, nullptr);
    if (m_VS)          vkDestroyShaderModule(m_Device, m_VS, nullptr);
    if (m_FSThresh)    vkDestroyShaderModule(m_Device, m_FSThresh, nullptr);
    if (m_FSBlur)      vkDestroyShaderModule(m_Device, m_FSBlur, nullptr);
    if (m_FSComp)      vkDestroyShaderModule(m_Device, m_FSComp, nullptr);
    if (m_PLA)         vkDestroyPipelineLayout(m_Device, m_PLA, nullptr);
    if (m_PLB)         vkDestroyPipelineLayout(m_Device, m_PLB, nullptr);
    if (m_DSL1)        vkDestroyDescriptorSetLayout(m_Device, m_DSL1, nullptr);
    if (m_DSL2)        vkDestroyDescriptorSetLayout(m_Device, m_DSL2, nullptr);
    if (m_Sampler)     vkDestroySampler(m_Device, m_Sampler, nullptr);
    if (m_SceneFB)         vkDestroyFramebuffer(m_Device, m_SceneFB, nullptr);
    if (m_SceneView)       vkDestroyImageView(m_Device, m_SceneView, nullptr);
    if (m_SceneImage)      vkDestroyImage(m_Device, m_SceneImage, nullptr);
    if (m_SceneMem)        vkFreeMemory(m_Device, m_SceneMem, nullptr);
    if (m_PrevSceneView)   vkDestroyImageView(m_Device, m_PrevSceneView, nullptr);
    if (m_PrevSceneImage)  vkDestroyImage(m_Device, m_PrevSceneImage, nullptr);
    if (m_PrevSceneMem)    vkFreeMemory(m_Device, m_PrevSceneMem, nullptr);

    for (int i = 0; i < 2; ++i) {
        if (m_BlurFB[i])     vkDestroyFramebuffer(m_Device, m_BlurFB[i], nullptr);
        if (m_BlurView[i])   vkDestroyImageView(m_Device, m_BlurView[i], nullptr);
        if (m_BlurImage[i])  vkDestroyImage(m_Device, m_BlurImage[i], nullptr);
        if (m_BlurMem[i])    vkFreeMemory(m_Device, m_BlurMem[i], nullptr);
    }
    if (m_SceneRP)     vkDestroyRenderPass(m_Device, m_SceneRP, nullptr);
    if (m_BlurRP)      vkDestroyRenderPass(m_Device, m_BlurRP, nullptr);

    *this = BloomVK();
}

}
