#include "renderer.h"

#include "bloom_vk.h"
#include "glass_vk.h"

#include <android/hardware_buffer.h>
#include "vulkan_wrapper.h"
#include <vulkan/vulkan_android.h>
#ifdef AIMGUI_LIVE2D
#include "live2d_vk_bridge.h"
#include <cstring>
// Registers the device with the dynamic-rendering shim (see
// vk_dynamic_rendering_shim.cpp) so vkCmdBeginRendering/EndRendering resolve.
extern "C" void aimgui_vk_set_device(VkDevice);
#endif

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <android/log.h>
#include <android/native_window.h>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>
#include <vector>

#define LOG_TAG "AImGui_VK"
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

namespace aimgui {

namespace {

static void check_vk(VkResult err) {
    if (err != VK_SUCCESS) {
        LOGE("VkResult = %d", err);
    }
}

class VKRenderer final : public IRenderer {
public:
    bool Init(ANativeWindow* window, int width, int height) override {
        m_Window = window;
        m_Width  = width;
        m_Height = height;

        if (InitVulkan() != 1) {
            LOGE("Vulkan loader unavailable: %s", dlerror());
            return false;
        }

        void* libvulkan = dlopen("libvulkan.so", RTLD_NOW);
        ImGui_ImplVulkan_LoadFunctions(0,
            [](const char* name, void* user) -> PFN_vkVoidFunction {
                return reinterpret_cast<PFN_vkVoidFunction>(dlsym(user, name));
            }, libvulkan);

        if (!CreateInstance()) return false;
        if (!SelectPhysicalDevice()) return false;
        if (!CreateLogicalDevice()) return false;
        if (!CreateDescriptorPool()) return false;
        if (!CreateSurfaceAndSwapchain()) return false;
#ifdef AIMGUI_LIVE2D
        // Command pool + offscreen image the Cubism renderer draws the model
        // into, plus the context struct handed to the Live2D layer.
        if (!CreateLive2DResources()) {
            LOGE("Live2D VK resources failed");
        }
#endif

        // Try to set up the bloom pipeline. If it fails for any reason the
        // renderer falls back to direct-to-swapchain ImGui rendering.
        if (m_Bloom.Init(m_Device, m_PhysicalDevice, m_DescPool,
                         m_WD->SurfaceFormat.format, m_Width, m_Height)) {
            if (!m_Bloom.BindToSwapchainRenderPass(m_WD->RenderPass)) {
                m_Bloom.Shutdown();
            }
#ifdef AIMGUI_LIVE2D
            else {
                m_Bloom.SetModelBackground(m_ModelView);
            }
#endif
        }

        m_Glass.Init(m_Device, m_DescPool,
                     m_Bloom.Ready() ? m_Bloom.GetSceneRenderPass() : m_WD->RenderPass);

        SetupImGuiBackend();

        // Now that ImGui's Vulkan impl has its descriptor pool wired up,
        // hand it the prev-scene image so dissolve particles sample real UI.
        if (m_Bloom.Ready()) m_Bloom.RegisterImGuiSnapshot();

        return true;
    }

    void NewFrame() override {
        if (m_SwapChainRebuild) RebuildSwapchain();
        ImGui_ImplVulkan_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)m_Width, (float)m_Height);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    void EndFrame() override {
        ImGui::Render();
        ImDrawData* draw = ImGui::GetDrawData();
        if (!draw || draw->DisplaySize.x <= 0 || draw->DisplaySize.y <= 0) return;
        Submit(draw);
    }

    void Shutdown() override {
        if (m_Device == VK_NULL_HANDLE) return;
        vkDeviceWaitIdle(m_Device);
#ifdef AIMGUI_LIVE2D
        DestroyLive2DResources();
#endif
        m_Bloom.Shutdown();
        m_Glass.Shutdown();
        ImGui_ImplVulkan_Shutdown();
        if (m_WD) {
            ImGui_ImplVulkanH_DestroyWindow(m_Instance, m_Device, m_WD, nullptr);
            delete m_WD;
            m_WD = nullptr;
        }
        if (m_DescPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Device, m_DescPool, nullptr);
        vkDestroyDevice(m_Device, nullptr);
        vkDestroyInstance(m_Instance, nullptr);
        m_Device = VK_NULL_HANDLE;
        m_Instance = VK_NULL_HANDLE;
    }

    const char* Name() const override { return "Vulkan"; }

    void SetBloomIntensity(float i) override { m_Bloom.SetIntensity(i); }

    unsigned long long GetSceneSnapshotID() override {
        return (unsigned long long)(uintptr_t)m_Bloom.GetSnapshotDescriptorSet();
    }

    void SetSnapshotFrozen(bool frozen) override { m_Bloom.SetSnapshotFrozen(frozen); }

    // Imports one of the screen mirror's AHardwareBuffers as a sampled image
    // and returns an ImTextureID for it. No copy: the VkImage is backed by the
    // very memory SurfaceFlinger composited into.
    //
    // AImageReader hands the same handful of buffers back round-robin, so
    // imports are cached by buffer pointer — re-importing per frame would mean
    // creating and destroying an image, a memory allocation and a descriptor
    // set 120 times a second.
    void SetGlassRects(const GlassRect* rects, int count,
                       int displayW, int displayH) override {
        m_GlassRects = rects;
        m_GlassCount = count;
        m_GlassW = displayW; m_GlassH = displayH;
    }

    unsigned long long ImportHardwareBuffer(AHardwareBuffer* ahb, int w, int h) override {
        if (!ahb || m_Device == VK_NULL_HANDLE) return 0;
        for (const auto& e : m_AhbCache)
            if (e.ahb == ahb) { m_ScreenView = e.view; return (unsigned long long)(uintptr_t)e.ds; }
        if (m_AhbCache.size() >= 8) return 0;   // reader cycles far fewer than this

        auto getProps = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)
            vkGetDeviceProcAddr(m_Device, "vkGetAndroidHardwareBufferPropertiesANDROID");
        if (!getProps) return 0;

        VkAndroidHardwareBufferFormatPropertiesANDROID fmtProps{};
        fmtProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
        VkAndroidHardwareBufferPropertiesANDROID props{};
        props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
        props.pNext = &fmtProps;
        if (getProps(m_Device, ahb, &props) != VK_SUCCESS) return 0;

        // The mirror allocates RGBA_8888, so Vulkan reports a real format and
        // no external-format/ycbcr sampler is needed. Bail rather than guess if
        // that ever stops being true.
        if (fmtProps.format == VK_FORMAT_UNDEFINED) return 0;

        VkExternalMemoryImageCreateInfo extImg{};
        extImg.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        extImg.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

        VkImageCreateInfo ic{};
        ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.pNext = &extImg;
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = fmtProps.format;
        ic.extent = { (uint32_t)w, (uint32_t)h, 1 };
        ic.mipLevels = 1;
        ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        AhbEntry e{};
        e.ahb = ahb;
        if (vkCreateImage(m_Device, &ic, nullptr, &e.image) != VK_SUCCESS) return 0;

        VkImportAndroidHardwareBufferInfoANDROID importInfo{};
        importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
        importInfo.buffer = ahb;

        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image = e.image;
        dedicated.pNext = &importInfo;

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext = &dedicated;
        mai.allocationSize = props.allocationSize;
        mai.memoryTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < 32; ++i) {
            if (props.memoryTypeBits & (1u << i)) { mai.memoryTypeIndex = i; break; }
        }
        if (mai.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(m_Device, &mai, nullptr, &e.mem) != VK_SUCCESS) {
            vkDestroyImage(m_Device, e.image, nullptr);
            return 0;
        }
        vkBindImageMemory(m_Device, e.image, e.mem, 0);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = e.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmtProps.format;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_Device, &vci, nullptr, &e.view) != VK_SUCCESS) {
            vkFreeMemory(m_Device, e.mem, nullptr);
            vkDestroyImage(m_Device, e.image, nullptr);
            return 0;
        }

        e.ds = ImGui_ImplVulkan_AddTexture(e.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!e.ds) {
            vkDestroyImageView(m_Device, e.view, nullptr);
            vkFreeMemory(m_Device, e.mem, nullptr);
            vkDestroyImage(m_Device, e.image, nullptr);
            return 0;
        }
        m_AhbCache.push_back(e);
        m_ScreenView = e.view;
        return (unsigned long long)(uintptr_t)e.ds;
    }

    void SetScenePreDraw(void (*fn)()) override { m_ScenePreDraw = fn; }

#ifdef AIMGUI_LIVE2D
    const Live2DVkContext* GetLive2DVkContext() override {
        return m_ModelImage != VK_NULL_HANDLE ? &m_L2DCtx : nullptr;
    }
#endif

private:
    bool CreateInstance() {
        const char* exts[] = { "VK_KHR_surface", "VK_KHR_android_surface",
                               "VK_KHR_get_physical_device_properties2",
                               "VK_KHR_external_memory_capabilities" };
        VkApplicationInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "AImGui";
#ifdef AIMGUI_LIVE2D
        // Cubism's Vulkan renderer uses dynamic rendering + synchronization2 +
        // extended dynamic state, all core in Vulkan 1.3.
        ai.apiVersion = VK_MAKE_VERSION(1, 3, 0);
#else
        ai.apiVersion = VK_MAKE_VERSION(1, 1, 0);
#endif

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledExtensionCount = (uint32_t)IM_ARRAYSIZE(exts);
        ci.ppEnabledExtensionNames = exts;
        return vkCreateInstance(&ci, nullptr, &m_Instance) == VK_SUCCESS;
    }

    bool SelectPhysicalDevice() {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(m_Instance, &n, nullptr);
        if (n == 0) return false;
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(m_Instance, &n, gpus.data());
        for (auto g : gpus) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(g, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                m_PhysicalDevice = g;
                return true;
            }
        }
        m_PhysicalDevice = gpus[0];
        return true;
    }

    bool CreateLogicalDevice() {
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qs(n);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &n, qs.data());
        for (uint32_t i = 0; i < n; ++i) {
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { m_QueueFamily = i; break; }
        }
        if (m_QueueFamily == UINT32_MAX) return false;

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = m_QueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;

#ifdef AIMGUI_LIVE2D
        // Cubism's Vulkan renderer records vkCmdBeginRendering (dynamic
        // rendering) and vkCmdSetCullModeEXT (extended dynamic state), and its
        // texture sampler enables anisotropy. Enable the matching device
        // extensions + features. All are core in 1.3 but the renderer resolves
        // the *EXT alias, so the extension must be enabled too.
        const char* dext[] = {
            "VK_KHR_swapchain",
            "VK_KHR_dynamic_rendering",
            "VK_EXT_extended_dynamic_state",
            // Importing the screen mirror's AHardwareBuffers as textures.
            // VK_ANDROID_external_memory_android_hardware_buffer pulls in
            // external-memory and ycbcr-conversion as dependencies, so they
            // have to be listed even though the mirror's RGBA_8888 buffers
            // never need a ycbcr sampler.
            "VK_KHR_external_memory",
            "VK_ANDROID_external_memory_android_hardware_buffer",
            "VK_EXT_queue_family_foreign",
            "VK_KHR_sampler_ycbcr_conversion",
            "VK_KHR_maintenance1",
            "VK_KHR_bind_memory2",
            "VK_KHR_get_memory_requirements2",
        };
        dci.enabledExtensionCount = (uint32_t)IM_ARRAYSIZE(dext);
        dci.ppEnabledExtensionNames = dext;

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{};
        eds.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        eds.extendedDynamicState = VK_TRUE;

        VkPhysicalDeviceVulkan13Features v13{};
        v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        v13.synchronization2 = VK_TRUE;
        v13.dynamicRendering = VK_TRUE;
        v13.pNext = &eds;

        VkPhysicalDeviceFeatures2 feats2{};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.features.samplerAnisotropy = VK_TRUE;
        feats2.pNext = &v13;
        dci.pNext = &feats2;
#else
        const char* dext[] = {
            "VK_KHR_swapchain",
            // Importing the screen mirror's AHardwareBuffers as textures.
            // VK_ANDROID_external_memory_android_hardware_buffer pulls in
            // external-memory and ycbcr-conversion as dependencies, so they
            // have to be listed even though the mirror's RGBA_8888 buffers
            // never need a ycbcr sampler.
            "VK_KHR_external_memory",
            "VK_ANDROID_external_memory_android_hardware_buffer",
            "VK_EXT_queue_family_foreign",
            "VK_KHR_sampler_ycbcr_conversion",
            "VK_KHR_maintenance1",
            "VK_KHR_bind_memory2",
            "VK_KHR_get_memory_requirements2",
        };
        dci.enabledExtensionCount = (uint32_t)IM_ARRAYSIZE(dext);
        dci.ppEnabledExtensionNames = dext;
#endif
        if (vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device) != VK_SUCCESS) return false;
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
#ifdef AIMGUI_LIVE2D
        aimgui_vk_set_device(m_Device);
#endif
        return true;
    }

    bool CreateDescriptorPool() {
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 64;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = sizes;
        return vkCreateDescriptorPool(m_Device, &ci, nullptr, &m_DescPool) == VK_SUCCESS;
    }

    bool CreateSurfaceAndSwapchain() {
        m_WD = new ImGui_ImplVulkanH_Window();

        VkAndroidSurfaceCreateInfoKHR sci{};
        sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        sci.window = m_Window;
        VkSurfaceKHR surface;
        if (vkCreateAndroidSurfaceKHR(m_Instance, &sci, nullptr, &surface) != VK_SUCCESS) return false;
        m_WD->Surface = surface;

        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, surface, &supported);
        if (!supported) return false;

        const VkFormat fmts[] = {
            VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8_UNORM,   VK_FORMAT_R8G8B8_UNORM,
        };
        m_WD->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            m_PhysicalDevice, surface, fmts, IM_ARRAYSIZE(fmts), VK_COLORSPACE_SRGB_NONLINEAR_KHR);

        // FIFO is hard vsync — the panel's vblank becomes our frame clock,
        // giving a flat refresh-rate-bound FPS without any CPU spin (the
        // wait happens inside vkAcquireNextImageKHR as the OS schedules us
        // off the CPU between frames). For target rates below the panel
        // refresh, main.cpp's drift-corrected sleep_until adds the gap.
        VkPresentModeKHR modes[] = { VK_PRESENT_MODE_FIFO_KHR };
        m_WD->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
            m_PhysicalDevice, surface, modes, IM_ARRAYSIZE(modes));

        ImGui_ImplVulkanH_CreateOrResizeWindow(
            m_Instance, m_PhysicalDevice, m_Device, m_WD,
            m_QueueFamily, nullptr, m_Width, m_Height, m_MinImageCount,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        return true;
    }

    void SetupImGuiBackend() {
        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance = m_Instance;
        ii.PhysicalDevice = m_PhysicalDevice;
        ii.Device = m_Device;
        ii.QueueFamily = m_QueueFamily;
        ii.Queue = m_Queue;
        ii.DescriptorPool = m_DescPool;
        // ImGui renders into the offscreen scene pass when bloom is wired up;
        // otherwise it draws straight into the swapchain.
        ii.PipelineInfoMain.RenderPass = m_Bloom.Ready() ? m_Bloom.GetSceneRenderPass()
                                                        : m_WD->RenderPass;
        ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ii.MinImageCount = m_MinImageCount;
        ii.ImageCount = m_WD->ImageCount;
        ii.CheckVkResultFn = check_vk;
        ImGui_ImplVulkan_Init(&ii);
    }

    void RebuildSwapchain() {
        int w = ANativeWindow_getWidth(m_Window);
        int h = ANativeWindow_getHeight(m_Window);
        if (w > 0 && h > 0) {
            usleep(200000);
            ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(
                m_Instance, m_PhysicalDevice, m_Device, m_WD,
                m_QueueFamily, nullptr, w, h, m_MinImageCount,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            m_WD->FrameIndex = 0;
            m_Width = w; m_Height = h;

            // Tear down and rebuild bloom against the new dimensions and
            // swapchain render pass. If anything fails we fall back to the
            // direct-to-swapchain path automatically.
            if (m_Bloom.Ready()) {
                m_Bloom.Shutdown();
                if (m_Bloom.Init(m_Device, m_PhysicalDevice, m_DescPool,
                                 m_WD->SurfaceFormat.format, m_Width, m_Height)) {
                    if (!m_Bloom.BindToSwapchainRenderPass(m_WD->RenderPass))
                        m_Bloom.Shutdown();
                    else {
                        m_Bloom.RegisterImGuiSnapshot();
#ifdef AIMGUI_LIVE2D
                        m_Bloom.SetModelBackground(m_ModelView);
#endif
                    }
                }
            }
        }
        m_SwapChainRebuild = false;
    }

    void RecordGlass(VkCommandBuffer cmd) {
        if (!m_Glass.Ready() || m_GlassCount <= 0 || m_ScreenView == VK_NULL_HANDLE) return;
        m_Glass.SetScreenImage(m_ScreenView);
        VkViewport vp{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
        VkRect2D   sc{ {0, 0}, { (uint32_t)m_Width, (uint32_t)m_Height } };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        m_Glass.Record(cmd, m_GlassW, m_GlassH, m_Width, m_Height, m_GlassRects, m_GlassCount);
    }

    void Submit(ImDrawData* draw) {
        VkResult err;
#ifdef AIMGUI_LIVE2D
        // Render the Live2D model into its offscreen image first. The Cubism
        // renderer self-submits to the graphics queue and waits idle, so this
        // must run before we begin recording this frame's command buffer. When
        // it returns the model image is in SHADER_READ_ONLY_OPTIMAL.
        bool haveModel = (m_ScenePreDraw != nullptr) && m_Bloom.Ready();
        if (m_ScenePreDraw) m_ScenePreDraw();
        m_Bloom.SetCompositeOverDest(haveModel);
#endif
        VkSemaphore acq = m_WD->FrameSemaphores[m_WD->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore done = m_WD->FrameSemaphores[m_WD->SemaphoreIndex].RenderCompleteSemaphore;
        err = vkAcquireNextImageKHR(m_Device, m_WD->Swapchain, UINT64_MAX, acq, VK_NULL_HANDLE, &m_WD->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR) { m_SwapChainRebuild = true; return; }

        ImGui_ImplVulkanH_Frame* fd = &m_WD->Frames[m_WD->FrameIndex];
        vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &fd->Fence);
        vkResetCommandPool(m_Device, fd->CommandPool, 0);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(fd->CommandBuffer, &bi);

        if (m_Bloom.Ready()) {
            // ImGui draws into the offscreen scene image, then threshold +
            // separable Gaussian blur populate the bloom image, and the
            // composite pass writes scene + bloom into the swapchain.
            m_Bloom.BeginScene(fd->CommandBuffer);
            RecordGlass(fd->CommandBuffer);
            ImGui_ImplVulkan_RenderDrawData(draw, fd->CommandBuffer);
            m_Bloom.EndSceneAndBlur(fd->CommandBuffer);

            VkRenderPassBeginInfo rpi{};
            rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpi.renderPass = m_WD->RenderPass;
            rpi.framebuffer = fd->Framebuffer;
            rpi.renderArea.extent.width  = m_WD->Width;
            rpi.renderArea.extent.height = m_WD->Height;
            rpi.clearValueCount = 1;
            rpi.pClearValues = &m_WD->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);
#ifdef AIMGUI_LIVE2D
            // Draw the (un-bloomed) model as the backdrop, then blend UI+bloom
            // over it. RecordCompositeDraw picks the alpha-blended pipeline
            // because SetCompositeOverDest(true) was set above.
            if (haveModel) m_Bloom.RecordModelBackground(fd->CommandBuffer);
#endif
            m_Bloom.RecordCompositeDraw(fd->CommandBuffer);
            vkCmdEndRenderPass(fd->CommandBuffer);

            // Stash a copy of the just-rendered scene for next frame's
            // dissolve particles to sample.
            m_Bloom.RecordSnapshotCopy(fd->CommandBuffer);
        } else {
            VkRenderPassBeginInfo rpi{};
            rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpi.renderPass = m_WD->RenderPass;
            rpi.framebuffer = fd->Framebuffer;
            rpi.renderArea.extent.width  = m_WD->Width;
            rpi.renderArea.extent.height = m_WD->Height;
            rpi.clearValueCount = 1;
            rpi.pClearValues = &m_WD->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);
            RecordGlass(fd->CommandBuffer);
            ImGui_ImplVulkan_RenderDrawData(draw, fd->CommandBuffer);
            vkCmdEndRenderPass(fd->CommandBuffer);
        }

        vkEndCommandBuffer(fd->CommandBuffer);

        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &acq;
        si.pWaitDstStageMask = &stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fd->CommandBuffer;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &done;
        vkQueueSubmit(m_Queue, 1, &si, fd->Fence);

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done;
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_WD->Swapchain;
        pi.pImageIndices = &m_WD->FrameIndex;
        err = vkQueuePresentKHR(m_Queue, &pi);
        if (err == VK_ERROR_OUT_OF_DATE_KHR) { m_SwapChainRebuild = true; return; }
        m_WD->SemaphoreIndex = (m_WD->SemaphoreIndex + 1) % m_WD->SemaphoreCount;
    }

#ifdef AIMGUI_LIVE2D
    VkFormat SelectDepthFormat() {
        const VkFormat cands[] = {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM,
        };
        for (VkFormat f : cands) {
            VkFormatProperties p;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, f, &p);
            if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return f;
        }
        return VK_FORMAT_D32_SFLOAT;
    }

    uint32_t FindMemType(uint32_t typeBits, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        return UINT32_MAX;
    }

    bool CreateLive2DResources() {
        // Dedicated command pool for Cubism (RESET flag: it re-records its
        // persistent update/draw command buffers every frame).
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_QueueFamily;
        if (vkCreateCommandPool(m_Device, &pci, nullptr, &m_L2DPool) != VK_SUCCESS) return false;

        m_DepthFormat = SelectDepthFormat();

        // Offscreen colour image the model is rendered into, then sampled as
        // the UI backdrop. Same size as the (square) surface.
        VkFormat fmt = m_WD->SurfaceFormat.format;
        VkImageCreateInfo ic{};
        ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = fmt;
        ic.extent = { (uint32_t)m_Width, (uint32_t)m_Height, 1 };
        ic.mipLevels = 1;
        ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_Device, &ic, nullptr, &m_ModelImage) != VK_SUCCESS) return false;

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(m_Device, m_ModelImage, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = FindMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mai.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(m_Device, &mai, nullptr, &m_ModelMem) != VK_SUCCESS) return false;
        vkBindImageMemory(m_Device, m_ModelImage, m_ModelMem, 0);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = m_ModelImage;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_Device, &vci, nullptr, &m_ModelView) != VK_SUCCESS) return false;

        // Clear it to transparent and leave it SHADER_READ_ONLY so it is
        // sampleable even before the first model draw (or if none loads).
        ClearModelImageToTransparent();
        FillLive2DContext();
        return true;
    }

    void ClearModelImageToTransparent() {
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = m_L2DPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer cb;
        if (vkAllocateCommandBuffers(m_Device, &cai, &cb) != VK_SUCCESS) return;
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);

        auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                           VkAccessFlags src, VkAccessFlags dst,
                           VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = oldL; b.newLayout = newL;
            b.srcAccessMask = src; b.dstAccessMask = dst;
            b.image = m_ModelImage;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkClearColorValue col{}; col.float32[0] = col.float32[1] = col.float32[2] = col.float32[3] = 0.0f;
        VkImageSubresourceRange rng{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(cb, m_ModelImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rng);
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vkEndCommandBuffer(cb);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        vkQueueSubmit(m_Queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Queue);
        vkFreeCommandBuffers(m_Device, m_L2DPool, 1, &cb);
    }

    void FillLive2DContext() {
        m_L2DCtx.instance       = m_Instance;
        m_L2DCtx.physicalDevice = m_PhysicalDevice;
        m_L2DCtx.device         = m_Device;
        m_L2DCtx.queue          = m_Queue;
        m_L2DCtx.queueFamily    = m_QueueFamily;
        m_L2DCtx.commandPool    = m_L2DPool;
        m_L2DCtx.imageCount     = (uint32_t)m_WD->ImageCount;
        m_L2DCtx.extent         = { (uint32_t)m_Width, (uint32_t)m_Height };
        m_L2DCtx.colorFormat    = m_WD->SurfaceFormat.format;
        m_L2DCtx.depthFormat    = m_DepthFormat;
        m_L2DCtx.modelImage     = m_ModelImage;
        m_L2DCtx.modelView      = m_ModelView;
    }

    void DestroyLive2DResources() {
        if (m_ModelView)  vkDestroyImageView(m_Device, m_ModelView, nullptr);
        if (m_ModelImage) vkDestroyImage(m_Device, m_ModelImage, nullptr);
        if (m_ModelMem)   vkFreeMemory(m_Device, m_ModelMem, nullptr);
        if (m_L2DPool)    vkDestroyCommandPool(m_Device, m_L2DPool, nullptr);
        m_ModelView = VK_NULL_HANDLE;
        m_ModelImage = VK_NULL_HANDLE;
        m_ModelMem = VK_NULL_HANDLE;
        m_L2DPool = VK_NULL_HANDLE;
    }

    VkCommandPool  m_L2DPool    = VK_NULL_HANDLE;
    VkImage        m_ModelImage = VK_NULL_HANDLE;
    VkImageView    m_ModelView  = VK_NULL_HANDLE;
    VkDeviceMemory m_ModelMem   = VK_NULL_HANDLE;
    VkFormat       m_DepthFormat = VK_FORMAT_UNDEFINED;
    Live2DVkContext m_L2DCtx;
#endif

    // One imported mirror buffer: the VkImage aliases SurfaceFlinger's memory,
    // so nothing here owns pixels — only the Vulkan objects wrapping them.
    struct AhbEntry {
        AHardwareBuffer* ahb   = nullptr;
        VkImage          image = VK_NULL_HANDLE;
        VkDeviceMemory   mem   = VK_NULL_HANDLE;
        VkImageView      view  = VK_NULL_HANDLE;
        VkDescriptorSet  ds    = VK_NULL_HANDLE;
    };
    std::vector<AhbEntry> m_AhbCache;
    VkImageView           m_ScreenView = VK_NULL_HANDLE;  // newest mirrored frame
    GlassVK               m_Glass;
    const GlassRect*      m_GlassRects = nullptr;
    int                   m_GlassCount = 0;
    int                   m_GlassW = 0, m_GlassH = 0;

    ANativeWindow* m_Window = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    VkDescriptorPool m_DescPool = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window* m_WD = nullptr;
    uint32_t m_QueueFamily = UINT32_MAX;
    int m_Width = 0;
    int m_Height = 0;
    int m_MinImageCount = 2;
    bool m_SwapChainRebuild = false;
    BloomVK m_Bloom;
    void (*m_ScenePreDraw)() = nullptr;
};

} // namespace

std::unique_ptr<IRenderer> MakeVKRenderer() {
    return std::unique_ptr<IRenderer>(new VKRenderer());
}

} // namespace aimgui
