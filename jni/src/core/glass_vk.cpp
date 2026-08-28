#include "glass_vk.h"

#include "glass_vk_spv.h"

#include <android/log.h>

namespace aimgui {
namespace {

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AImGui", __VA_ARGS__)

struct Push {
    float screen[4];
    float params[4];
    float tint[4];
    float params2[4];
    float shapes[kMaxMergedShapes * 4];
};
static_assert(sizeof(Push) <= 128, "push constants must fit the guaranteed 128 bytes");

VkShaderModule MakeModule(VkDevice d, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    si.codeSize = bytes;
    si.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(d, &si, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

}

bool GlassVK::Init(VkDevice device, VkDescriptorPool pool, VkRenderPass renderPass) {
    m_Device = device;
    m_Pool   = pool;

    m_VS = MakeModule(device, glass_vk_spv::kVS, sizeof(glass_vk_spv::kVS));
    m_FS = MakeModule(device, glass_vk_spv::kFS, sizeof(glass_vk_spv::kFS));
    if (!m_VS || !m_FS) { LOGE("glass: shader modules"); Shutdown(); return false; }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(device, &si, nullptr, &m_Sampler) != VK_SUCCESS) { Shutdown(); return false; }

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 1;
    dlci.pBindings = &b;
    if (vkCreateDescriptorSetLayout(device, &dlci, nullptr, &m_DSL) != VK_SUCCESS) { Shutdown(); return false; }

    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &m_DSL;
    if (vkAllocateDescriptorSets(device, &dai, &m_DS) != VK_SUCCESS) { Shutdown(); return false; }
    // A second set for the controls' pass. Not fatal if the pool is out: the
    // controls simply keep painting their own edge.
    if (vkAllocateDescriptorSets(device, &dai, &m_DSWidget) != VK_SUCCESS)
        m_DSWidget = VK_NULL_HANDLE;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(Push);
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_DSL;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_Layout) != VK_SUCCESS) { Shutdown(); return false; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_VS;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_FS;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    const VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = m_Layout;
    gp.renderPass = renderPass;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_Pipe) != VK_SUCCESS) {
        LOGE("glass: pipeline");
        Shutdown();
        return false;
    }

    m_Ready = true;
    return true;
}

void GlassVK::SetScreenImage(VkImageView view) {
    if (!m_Ready || view == VK_NULL_HANDLE || view == m_ScreenView) return;
    m_ScreenView = view;

    VkDescriptorImageInfo ii{};
    ii.sampler = m_Sampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m_DS;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_Device, 1, &w, 0, nullptr);
}

void GlassVK::SetWidgetImage(VkImageView view) {
    if (!m_Ready || m_DSWidget == VK_NULL_HANDLE) return;
    if (view == VK_NULL_HANDLE || view == m_WidgetView) return;
    m_WidgetView = view;

    VkDescriptorImageInfo ii{};
    ii.sampler = m_Sampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = m_DSWidget;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_Device, 1, &w, 0, nullptr);
}

// One draw per control, each a single shape, so they never compete for the four
// slots a merged body has. Screen and surface are the same here: the texture is
// a copy of this surface, not the mirror of a differently sized display.
void GlassVK::RecordWidgets(VkCommandBuffer cmd, int surfaceW, int surfaceH,
                            const GlassRect* rects, int count) {
    if (!m_Ready || m_WidgetView == VK_NULL_HANDLE || m_DSWidget == VK_NULL_HANDLE) return;
    if (count <= 0 || surfaceW <= 0 || surfaceH <= 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, 1,
                            &m_DSWidget, 0, nullptr);

    for (int i = 0; i < count; ++i) {
        const GlassRect& r = rects[i];
        if (r.w < 2.0f || r.h < 2.0f || r.alpha <= 0.001f) continue;
        Push p{};
        p.screen[0] = (float)surfaceW; p.screen[1] = (float)surfaceH;
        p.screen[2] = (float)surfaceW; p.screen[3] = (float)surfaceH;
        p.params[0] = r.rounding; p.params[1] = r.edgeWidth;
        p.params[2] = r.bend;     p.params[3] = r.alpha;
        p.tint[0] = r.tintR; p.tint[1] = r.tintG; p.tint[2] = r.tintB; p.tint[3] = r.tintA;
        p.params2[0] = r.blur;
        p.params2[1] = r.lightX;
        p.params2[2] = r.lightY;
        p.params2[3] = 0.0f;
        p.shapes[0] = r.x + r.w * 0.5f;
        p.shapes[1] = r.y + r.h * 0.5f;
        p.shapes[2] = r.w * 0.5f;
        p.shapes[3] = r.h * 0.5f;
        vkCmdPushConstants(cmd, m_Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Push), &p);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

void GlassVK::Record(VkCommandBuffer cmd, int screenW, int screenH,
                     int surfaceW, int surfaceH,
                     const GlassRect* rects, int count) {
    if (!m_Ready || m_ScreenView == VK_NULL_HANDLE) return;
    if (count <= 0 || screenW <= 0 || screenH <= 0) return;
    if (surfaceW <= 0 || surfaceH <= 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, 1, &m_DS, 0, nullptr);

    for (int gi = 0; gi < kMaxGlassGroups; ++gi) {
        GlassGroup g;
        const GlassRect* lead = nullptr;
        if (!BuildGlassGroup(rects, count, &g, gi, &lead) || !lead) continue;

        const GlassRect& r = *lead;
        Push p{};
        p.screen[0] = (float)screenW;  p.screen[1] = (float)screenH;
        p.screen[2] = (float)surfaceW; p.screen[3] = (float)surfaceH;
        p.params[0] = r.rounding; p.params[1] = r.edgeWidth;
        p.params[2] = r.bend;     p.params[3] = r.alpha;
        p.tint[0] = r.tintR; p.tint[1] = r.tintG; p.tint[2] = r.tintB; p.tint[3] = r.tintA;
        p.params2[0] = r.blur;
        p.params2[1] = r.lightX; p.params2[2] = r.lightY;
        p.params2[3] = r.merge;
        for (int i = 0; i < kMaxMergedShapes * 4; ++i) p.shapes[i] = g.shapes[i];
        vkCmdPushConstants(cmd, m_Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(p), &p);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

void GlassVK::Shutdown() {
    if (m_Device == VK_NULL_HANDLE) return;
    if (m_Pipe)    vkDestroyPipeline(m_Device, m_Pipe, nullptr);
    if (m_Layout)  vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
    if (m_DSL)     vkDestroyDescriptorSetLayout(m_Device, m_DSL, nullptr);
    if (m_Sampler) vkDestroySampler(m_Device, m_Sampler, nullptr);
    if (m_VS)      vkDestroyShaderModule(m_Device, m_VS, nullptr);
    if (m_FS)      vkDestroyShaderModule(m_Device, m_FS, nullptr);
    m_Pipe = VK_NULL_HANDLE; m_Layout = VK_NULL_HANDLE; m_DSL = VK_NULL_HANDLE;
    m_Sampler = VK_NULL_HANDLE; m_VS = VK_NULL_HANDLE; m_FS = VK_NULL_HANDLE;
    m_DS = VK_NULL_HANDLE; m_ScreenView = VK_NULL_HANDLE;
    m_Ready = false;
}

}
