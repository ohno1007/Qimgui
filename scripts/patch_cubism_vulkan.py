#!/usr/bin/env python3
# Patch the (transiently-copied) Cubism SDK Vulkan renderer for this project:
#
#  1. CubismRenderer_Vulkan.cpp: the SDK loads its compiled SPIR-V with
#     std::ifstream on a path relative to the process CWD ("FrameworkShaders/
#     xxx.spv"). We ship the .spv embedded in the binary instead, so route
#     CreateShaderModule through aimgui_l2d_load_asset() (implemented in the
#     glue) which serves them by basename.
#
#  2. CubismClass_Vulkan.cpp: the SHADER_READ_ONLY layout transition sets its
#     destination stage to VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, which
#     requires the ray-tracing pipeline feature — absent on mobile GPUs and an
#     invalid barrier there. Drop it back to FRAGMENT_SHADER.
#
# Usage: patch_cubism_vulkan.py <FrameworkRoot>
import os, sys

def patch(path, olds_news):
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    for old, new, required in olds_news:
        if old in src:
            src = src.replace(old, new, 1)
        elif required:
            sys.exit("patch_cubism_vulkan: pattern not found in %s:\n%s" % (path, old))
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print("patched", path)

def main():
    root = sys.argv[1]
    vk = os.path.join(root, "src", "Rendering", "Vulkan")

    renderer = os.path.join(vk, "CubismRenderer_Vulkan.cpp")
    old_body = '''    std::ifstream file(filename.GetRawString(), std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        CubismLogError("failed to open file!");
        return NULL;
    }

    csmInt32 fileSize = (csmInt32)file.tellg();
    csmVector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.GetPtr(), fileSize);
    file.close();
'''
    new_body = '''    // AImGui: SPIR-V is embedded in the binary and served by basename.
    unsigned aimgui_sz = 0;
    const unsigned char* aimgui_data = aimgui_l2d_load_asset(filename.GetRawString(), &aimgui_sz);
    if (!aimgui_data)
    {
        CubismLogError("failed to open file!");
        return NULL;
    }
    csmInt32 fileSize = (csmInt32)aimgui_sz;
    const char* aimgui_ptr = reinterpret_cast<const char*>(aimgui_data);
'''
    patch(renderer, [
        # Declare the loader once, right before the function that uses it.
        ('VkShaderModule CubismPipeline_Vulkan::PipelineResource::CreateShaderModule',
         'extern "C" const unsigned char* aimgui_l2d_load_asset(const char*, unsigned*);\n\n'
         'VkShaderModule CubismPipeline_Vulkan::PipelineResource::CreateShaderModule',
         True),
        (old_body, new_body, True),
        ('createInfo.pCode = reinterpret_cast<const csmUint32*>(buffer.GetPtr());',
         'createInfo.pCode = reinterpret_cast<const csmUint32*>(aimgui_ptr);',
         True),
    ])

    klass = os.path.join(vk, "CubismClass_Vulkan.cpp")
    patch(klass, [
        ('        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;\n'
         '        destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;\n',
         '        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;\n',
         True),
    ])

if __name__ == "__main__":
    main()
