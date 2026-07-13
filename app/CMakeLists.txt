# AImGui APK — non-root native build.
#
# Reuses the renderers, bloom, CJK font loader and the ImGui UI from the repo's
# jni/ tree (../jni), and adds a NativeActivity entry point (src/main/cpp).
# The root-only platform glue (SurfaceFlinger window creation, /dev/input touch
# and keyboard readers, window_session) is deliberately left out — the Activity
# supplies the window and the framework supplies input.
cmake_minimum_required(VERSION 3.22.1)
project(aimgui LANGUAGES C CXX)

# Optional Live2D Cubism model layer (the floating character). OFF unless the
# Gradle build passes -DAIMGUI_LIVE2D=ON (see app/build.gradle). Requires the
# user-supplied Cubism SDK under ../third_party and a model under
# ../live2d/models — see docs/LIVE2D.md.
option(AIMGUI_LIVE2D "Build the APK with the Live2D Cubism character" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Mirror the size-oriented flags of the ELF build (minus -s, so the APK keeps
# symbols the toolchain wants; the AGP release build strips separately).
add_compile_options(
    -Os
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -fno-rtti)
add_compile_definitions(SURFACE_LOG_ENABLE=0)
add_link_options(-Wl,--gc-sections)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(JNI  ${REPO}/jni)

# ── Prebuilt Dear ImGui (v1.92.8, incl. GL3 + Vulkan backends) ───────────
add_library(imgui STATIC IMPORTED GLOBAL)
set_target_properties(imgui PROPERTIES
    IMPORTED_LOCATION ${REPO}/prebuilt/${ANDROID_ABI}/libimgui.a)
target_include_directories(imgui INTERFACE
    ${JNI}/include/imgui
    ${JNI}/include/imgui/backends)

# ── NativeActivity glue (shipped with the NDK) ───────────────────────────
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# ── aimgui_core: renderers, bloom, CJK font ──────────────────────────────
# Same translation units as the ELF build's aimgui_core, minus the
# root-dependent keyboard_input.cpp and window_session.cpp.
add_library(aimgui_core STATIC
    ${JNI}/src/core/renderer_factory.cpp
    ${JNI}/src/core/renderer_gl.cpp
    ${JNI}/src/core/renderer_vk.cpp
    ${JNI}/src/core/vulkan_wrapper.cpp
    ${JNI}/src/core/bloom_gl.cpp
    ${JNI}/src/core/bloom_vk.cpp
    ${JNI}/src/core/font.cpp)
target_compile_definitions(aimgui_core PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(aimgui_core
    PUBLIC  ${JNI}/src
    PRIVATE ${JNI}/include)
target_link_libraries(aimgui_core PUBLIC imgui)

# ── The NativeActivity shared library the APK loads ──────────────────────
add_library(aimgui SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/android_main.cpp
    ${JNI}/src/ui/ui.cpp
    ${JNI}/src/ui/main_ui.cpp)
target_include_directories(aimgui PRIVATE ${JNI}/include ${JNI}/src)
target_link_libraries(aimgui PRIVATE
    aimgui_core imgui native_app_glue
    android log EGL GLESv3)
# native_app_glue provides ANativeActivity_onCreate; --gc-sections would drop
# it since nothing in this .so references it directly, so pin it.
target_link_options(aimgui PRIVATE -u ANativeActivity_onCreate)

# ── Optional Live2D Cubism layer ─────────────────────────────────────────
# Mirrors the ELF build's AIMGUI_LIVE2D wiring (root CMakeLists.txt), adapted
# to link into the NativeActivity shared library instead of the executable.
# The Cubism renderer is Vulkan; the model + compiled SPIR-V are embedded into
# libaimgui.so at build time so the APK is self-contained (no adb push needed).
if(AIMGUI_LIVE2D)
    set(LIVE2D_CORE      ${REPO}/third_party/Core)
    set(LIVE2D_FRAMEWORK ${REPO}/third_party/CubismNativeFramework)

    if(NOT EXISTS ${LIVE2D_CORE}/include/Live2DCubismCore.h)
        message(FATAL_ERROR "AIMGUI_LIVE2D=ON but Cubism Core is missing at "
            "${LIVE2D_CORE}. Copy the SDK's Core/ there — see docs/LIVE2D.md.")
    endif()
    if(NOT EXISTS ${LIVE2D_FRAMEWORK}/CMakeLists.txt)
        message(FATAL_ERROR "AIMGUI_LIVE2D=ON but Cubism Framework is missing at "
            "${LIVE2D_FRAMEWORK}. Copy the SDK's Framework/ there — see docs/LIVE2D.md.")
    endif()

    # Cubism Core (prebuilt static lib from the SDK).
    add_library(Live2DCubismCore STATIC IMPORTED GLOBAL)
    set_target_properties(Live2DCubismCore PROPERTIES
        IMPORTED_LOCATION ${LIVE2D_CORE}/lib/android/${ANDROID_ABI}/libLive2DCubismCore.a)

    # Cubism Framework (built from source; Vulkan renderer for Android).
    set(FRAMEWORK_SOURCE Vulkan CACHE STRING "" FORCE)
    set(FRAMEWORK_DEFINITIOINS "" CACHE STRING "" FORCE)
    add_subdirectory(${LIVE2D_FRAMEWORK} CubismFramework)
    target_include_directories(Framework PUBLIC ${LIVE2D_CORE}/include)
    # CubismUserModel::GetRenderer() uses dynamic_cast → RTTI required here.
    target_compile_options(Framework PRIVATE -frtti)
    target_link_libraries(Framework PUBLIC vulkan)

    # Compile the Cubism Vulkan GLSL shaders to SPIR-V (target FrameworkShaders,
    # needs glslc on PATH — the NDK ships it under shader-tools/).
    add_subdirectory(${LIVE2D_FRAMEWORK}/src/Rendering/Vulkan/Shaders FrameworkShaders)
    set(_l2d_spv_dir ${CMAKE_BINARY_DIR}/FrameworkShaders/compiledShaders)

    # Embed the compiled SPIR-V (served by basename) and the first bundled model
    # into the .so via a generated .incbin blob so the app runs standalone.
    set(_l2d_gen_s ${CMAKE_BINARY_DIR}/live2d_embedded.S)
    set(_l2d_gen_c ${CMAKE_BINARY_DIR}/live2d_embedded.cpp)
    set(_l2d_gen_args ${_l2d_gen_s} ${_l2d_gen_c} --spv ${_l2d_spv_dir})
    set(_l2d_dep_files ${compiled_shaders_framework})
    set(_l2d_voice_dir ${REPO}/live2d/voice)
    if(EXISTS ${_l2d_voice_dir})
        list(APPEND _l2d_gen_args --voice ${_l2d_voice_dir})
        file(GLOB _l2d_voice_files ${_l2d_voice_dir}/*)
        list(APPEND _l2d_dep_files ${_l2d_voice_files})
    endif()
    file(GLOB _l2d_model3 ${REPO}/live2d/models/*/*.model3.json)
    if(_l2d_model3)
        list(GET _l2d_model3 0 _l2d_m3first)
        get_filename_component(_l2d_model_dir ${_l2d_m3first} DIRECTORY)
        list(APPEND _l2d_gen_args --model ${_l2d_model_dir})
        file(GLOB_RECURSE _l2d_model_files ${_l2d_model_dir}/*)
        list(APPEND _l2d_dep_files ${_l2d_model_files})
        message(STATUS "AImGui APK: embedding Live2D SPIR-V + model ${_l2d_model_dir}")
    else()
        message(STATUS "AImGui APK: embedding Live2D SPIR-V (no bundled model)")
    endif()
    add_custom_command(
        OUTPUT ${_l2d_gen_s} ${_l2d_gen_c}
        COMMAND ${CMAKE_COMMAND} -E env python3
                ${REPO}/scripts/embed_live2d_model.py ${_l2d_gen_args}
        DEPENDS ${REPO}/scripts/embed_live2d_model.py FrameworkShaders ${_l2d_dep_files}
        COMMENT "Embedding Live2D SPIR-V + model")
    enable_language(ASM)

    add_library(aimgui_live2d STATIC
        ${JNI}/src/live2d/live2d_view.cpp
        ${JNI}/src/live2d/live2d_model.cpp
        ${JNI}/src/live2d/live2d_texture.cpp
        ${JNI}/src/live2d/live2d_audio.cpp
        ${JNI}/src/live2d/vk_dynamic_rendering_shim.cpp
        ${_l2d_gen_s} ${_l2d_gen_c})
    target_compile_options(aimgui_live2d PRIVATE -frtti)
    target_compile_definitions(aimgui_live2d PRIVATE AIMGUI_LIVE2D AIMGUI_REAL_VULKAN)
    target_include_directories(aimgui_live2d
        PUBLIC  ${JNI}/src ${JNI}/include
        PRIVATE ${LIVE2D_CORE}/include ${LIVE2D_FRAMEWORK}/src)
    target_link_libraries(aimgui_live2d
        PUBLIC  Framework Live2DCubismCore
        PRIVATE vulkan android log jnigraphics aaudio)

    # The renderer TUs in aimgui_core (renderer_vk / bloom_vk) must switch to
    # the real libvulkan (prototypes on) so they interoperate with Cubism's
    # Vulkan renderer instead of the dlsym wrapper. Propagate to everything
    # that links aimgui_core.
    target_compile_definitions(aimgui_core PUBLIC AIMGUI_LIVE2D AIMGUI_REAL_VULKAN)
    target_link_libraries(aimgui_core PUBLIC vulkan)

    target_compile_definitions(aimgui PRIVATE AIMGUI_LIVE2D)
    target_link_libraries(aimgui PRIVATE aimgui_live2d vulkan)
    message(STATUS "AImGui APK: Live2D Cubism support ENABLED (Vulkan)")
endif()
