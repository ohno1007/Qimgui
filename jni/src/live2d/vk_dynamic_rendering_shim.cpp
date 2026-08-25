#include <vulkan/vulkan.h>

extern "C" {

static VkDevice g_shim_device = VK_NULL_HANDLE;

void aimgui_vk_set_device(VkDevice device) { g_shim_device = device; }

static PFN_vkCmdBeginRendering g_pBegin = nullptr;
static PFN_vkCmdEndRendering   g_pEnd   = nullptr;

static PFN_vkVoidFunction Resolve(const char* core, const char* khr) {
    PFN_vkVoidFunction fn = vkGetDeviceProcAddr(g_shim_device, core);
    if (!fn) fn = vkGetDeviceProcAddr(g_shim_device, khr);
    return fn;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(VkCommandBuffer commandBuffer,
                                               const VkRenderingInfo* pRenderingInfo) {
    if (!g_pBegin)
        g_pBegin = reinterpret_cast<PFN_vkCmdBeginRendering>(
            Resolve("vkCmdBeginRendering", "vkCmdBeginRenderingKHR"));
    g_pBegin(commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer commandBuffer) {
    if (!g_pEnd)
        g_pEnd = reinterpret_cast<PFN_vkCmdEndRendering>(
            Resolve("vkCmdEndRendering", "vkCmdEndRenderingKHR"));
    g_pEnd(commandBuffer);
}

}
