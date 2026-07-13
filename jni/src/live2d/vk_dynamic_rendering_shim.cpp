// Android's libvulkan.so stub only exports Vulkan 1.0/1.1 core commands for
// static linking; 1.3 commands like vkCmdBeginRendering/vkCmdEndRendering must
// be resolved at runtime via vkGetDeviceProcAddr. Cubism's Vulkan renderer
// calls them as plain prototypes, which fails to link on Android.
//
// Provide the missing symbols here: each lazily resolves the real entry point
// (core name first, KHR alias as a fallback for 1.1/1.2 devices that enable
// VK_KHR_dynamic_rendering) and forwards to it. renderer_vk registers the
// device via aimgui_vk_set_device() right after creating it.
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

} // extern "C"
