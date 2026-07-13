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

# Optional Live2D Cubism model layer. OFF by default; requires the user-supplied
# Cubism SDK (see docs/LIVE2D.md). When OFF, nothing below changes.
option(AIMGUI_LIVE2D "Build with Live2D Cubism model support" OFF)

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

# ── Optional Live2D Cubism layer ─────────────────────────────────────────
# Requires the user to drop the Cubism SDK's Core/ and Framework/ into
# third_party/ (same SDK package → matching versions). See docs/LIVE2D.md.
if(AIMGUI_LIVE2D)
    set(LIVE2D_CORE      ${CMAKE_SOURCE_DIR}/third_party/Core)
    set(LIVE2D_FRAMEWORK ${CMAKE_SOURCE_DIR}/third_party/CubismNativeFramework)

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

    # Cubism Framework (built from source; OpenGL ES renderer for Android).
    set(FRAMEWORK_SOURCE OpenGL CACHE STRING "" FORCE)
    set(FRAMEWORK_DEFINITIOINS CSM_TARGET_ANDROID_ES2 CACHE STRING "" FORCE)
    add_subdirectory(${LIVE2D_FRAMEWORK} CubismFramework)
    target_compile_definitions(Framework PUBLIC CSM_TARGET_ANDROID_ES2)
    target_include_directories(Framework PUBLIC ${LIVE2D_CORE}/include)

    add_library(aimgui_live2d STATIC
        ${JNI}/src/live2d/live2d_view.cpp
        ${JNI}/src/live2d/live2d_model.cpp
        ${JNI}/src/live2d/live2d_texture.cpp)
    target_include_directories(aimgui_live2d
        PUBLIC  ${JNI}/src ${JNI}/include
        PRIVATE ${LIVE2D_CORE}/include ${LIVE2D_FRAMEWORK}/src)
    target_link_libraries(aimgui_live2d
        PUBLIC  Framework Live2DCubismCore
        PRIVATE GLESv3 EGL android log jnigraphics)

    target_compile_definitions(AImGui PRIVATE AIMGUI_LIVE2D)
    target_link_libraries(AImGui PRIVATE aimgui_live2d)
    message(STATUS "AImGui: Live2D Cubism support ENABLED")
endif()
