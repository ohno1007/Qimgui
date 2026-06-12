# AImGui: minimal Dear ImGui Android ARM64 ELF.
#
# Build via build.sh (which configures the NDK toolchain), or manually:
#
#   cmake -S . -B build/arm64-v8a \
#       -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
#       -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
#       -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/arm64-v8a -j
cmake_minimum_required(VERSION 3.18)
project(AImGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Size-optimised flags shared by every target — mirror what the old
# jni/Application.mk used so the resulting ELF stays small.
# Note: -flto is intentionally omitted from both compile and link. The
# old Application.mk listed it under APP_LDFLAGS only, so no objects ever
# carried LTO IR and link-time LTO was a no-op. Turning real LTO on broke
# ANativeWindowCreator::ProcessMirrorDisplay() (the hidden-visibility,
# heavy-static-state mirror path crashed when a screen-recording virtual
# display appeared). Keep the no-op for byte-for-byte parity with what
# shipped before.
add_compile_options(
    -Os
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fno-rtti)
add_compile_definitions(SURFACE_LOG_ENABLE=0)
add_link_options(-Wl,--gc-sections -Wl,--icf=all -Wl,-s)

set(JNI ${CMAKE_SOURCE_DIR}/jni)

# ── Prebuilt Dear ImGui ──────────────────────────────────────────────────
# Shipped under prebuilt/$ABI/libimgui.a; only public headers live in-tree.
add_library(imgui STATIC IMPORTED GLOBAL)
set_target_properties(imgui PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/prebuilt/${ANDROID_ABI}/libimgui.a)
target_include_directories(imgui INTERFACE
    ${JNI}/include/imgui
    ${JNI}/include/imgui/backends)

# ── aimgui_platform: ANativeWindow + Touch helper ────────────────────────
add_library(aimgui_platform STATIC
    ${JNI}/src/platform/TouchHelperA.cpp)
target_include_directories(aimgui_platform PUBLIC ${JNI}/src)
target_link_libraries(aimgui_platform PUBLIC imgui)

# ── aimgui_core: renderers, bloom, font, keyboard, window-session ────────
add_library(aimgui_core STATIC
    ${JNI}/src/core/renderer_factory.cpp
    ${JNI}/src/core/renderer_gl.cpp
    ${JNI}/src/core/renderer_vk.cpp
    ${JNI}/src/core/vulkan_wrapper.cpp
    ${JNI}/src/core/bloom_gl.cpp
    ${JNI}/src/core/bloom_vk.cpp
    ${JNI}/src/core/font.cpp
    ${JNI}/src/core/keyboard_input.cpp
    ${JNI}/src/core/window_session.cpp)
target_compile_definitions(aimgui_core PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(aimgui_core
    PUBLIC  ${JNI}/src
    PRIVATE ${JNI}/include)
target_link_libraries(aimgui_core PUBLIC imgui aimgui_platform)

# ── Executable ───────────────────────────────────────────────────────────
add_executable(AImGui
    ${JNI}/src/main.cpp
    ${JNI}/src/ui/ui.cpp
    ${JNI}/src/ui/main_ui.cpp)
target_include_directories(AImGui PRIVATE ${JNI}/include ${JNI}/src)
target_link_libraries(AImGui PRIVATE
    aimgui_core aimgui_platform imgui
    log android EGL GLESv3)
