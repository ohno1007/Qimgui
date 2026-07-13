# AImGui APK — non-root native build.
#
# Reuses the renderers, bloom, CJK font loader and the ImGui UI from the repo's
# jni/ tree (../jni), and adds a NativeActivity entry point (src/main/cpp).
# The root-only platform glue (SurfaceFlinger window creation, /dev/input touch
# and keyboard readers, window_session) is deliberately left out — the Activity
# supplies the window and the framework supplies input.
cmake_minimum_required(VERSION 3.22.1)
project(aimgui LANGUAGES C CXX)

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
