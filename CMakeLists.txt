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
    ${JNI}/src/core/window_session.cpp
    ${JNI}/src/core/screen_mirror.cpp
    ${JNI}/src/core/sensor_tilt.cpp
    ${JNI}/src/core/text_outline.cpp
    ${JNI}/src/core/haptics.cpp
    ${JNI}/src/core/config.cpp
    ${JNI}/src/core/glass_gl.cpp
    ${JNI}/src/core/glass_vk.cpp)
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

    # Cubism Framework (built from source; Vulkan renderer for Android).
    set(FRAMEWORK_SOURCE Vulkan CACHE STRING "" FORCE)
    set(FRAMEWORK_DEFINITIOINS "" CACHE STRING "" FORCE)
    add_subdirectory(${LIVE2D_FRAMEWORK} CubismFramework)
    target_include_directories(Framework PUBLIC ${LIVE2D_CORE}/include)
    # CubismUserModel::GetRenderer() uses dynamic_cast, so RTTI is required for
    # the Framework and our glue (the rest of the project builds -fno-rtti).
    target_compile_options(Framework PRIVATE -frtti)
    target_link_libraries(Framework PUBLIC vulkan)

    # Compile the Cubism Vulkan GLSL shaders to SPIR-V. The SDK ships a
    # CMakeLists under Shaders/ that enumerates every (mask × blend-mode)
    # permutation the renderer loads and compiles them with glslc into
    # compiledShaders/*.spv (target: FrameworkShaders). Requires glslc on PATH
    # (the NDK ships it under shader-tools/).
    add_subdirectory(${LIVE2D_FRAMEWORK}/src/Rendering/Vulkan/Shaders FrameworkShaders)
    set(_l2d_spv_dir ${CMAKE_BINARY_DIR}/FrameworkShaders/compiledShaders)

    # Embed the compiled SPIR-V (served to the renderer by basename) and, if
    # present, the first bundled model — into the binary via a generated
    # .incbin blob so the app runs standalone.
    set(_l2d_gen_s ${CMAKE_BINARY_DIR}/live2d_embedded.S)
    set(_l2d_gen_c ${CMAKE_BINARY_DIR}/live2d_embedded.cpp)
    set(_l2d_gen_args ${_l2d_gen_s} ${_l2d_gen_c} --spv ${_l2d_spv_dir})
    set(_l2d_dep_files ${compiled_shaders_framework})
    # Embed bundled voice clips (16-bit PCM WAV), if any.
    set(_l2d_voice_dir ${CMAKE_SOURCE_DIR}/live2d/voice)
    if(EXISTS ${_l2d_voice_dir})
        list(APPEND _l2d_gen_args --voice ${_l2d_voice_dir})
        file(GLOB _l2d_voice_files ${_l2d_voice_dir}/*)
        list(APPEND _l2d_dep_files ${_l2d_voice_files})
    endif()
    file(GLOB _l2d_model3 ${CMAKE_SOURCE_DIR}/live2d/models/*/*.model3.json)
    if(_l2d_model3)
        list(GET _l2d_model3 0 _l2d_m3first)
        get_filename_component(_l2d_model_dir ${_l2d_m3first} DIRECTORY)
        list(APPEND _l2d_gen_args --model ${_l2d_model_dir})
        file(GLOB_RECURSE _l2d_model_files ${_l2d_model_dir}/*)
        list(APPEND _l2d_dep_files ${_l2d_model_files})
        message(STATUS "AImGui: embedding Live2D SPIR-V + model ${_l2d_model_dir}")
    else()
        message(STATUS "AImGui: embedding Live2D SPIR-V (no bundled model)")
    endif()
    add_custom_command(
        OUTPUT ${_l2d_gen_s} ${_l2d_gen_c}
        COMMAND ${CMAKE_COMMAND} -E env python3
                ${CMAKE_SOURCE_DIR}/scripts/embed_live2d_model.py ${_l2d_gen_args}
        DEPENDS ${CMAKE_SOURCE_DIR}/scripts/embed_live2d_model.py FrameworkShaders ${_l2d_dep_files}
        COMMENT "Embedding Live2D SPIR-V + model")
    enable_language(ASM)
    set(_l2d_embed_srcs ${_l2d_gen_s} ${_l2d_gen_c})

    add_library(aimgui_live2d STATIC
        ${JNI}/src/live2d/live2d_view.cpp
        ${JNI}/src/live2d/live2d_model.cpp
        ${JNI}/src/live2d/live2d_texture.cpp
        ${JNI}/src/live2d/live2d_audio.cpp
        ${JNI}/src/live2d/vk_dynamic_rendering_shim.cpp
        ${_l2d_embed_srcs})
    target_compile_options(aimgui_live2d PRIVATE -frtti) # GetRenderer<>() dynamic_cast
    target_compile_definitions(aimgui_live2d PRIVATE AIMGUI_LIVE2D AIMGUI_REAL_VULKAN)
    target_include_directories(aimgui_live2d
        PUBLIC  ${JNI}/src ${JNI}/include
        PRIVATE ${LIVE2D_CORE}/include ${LIVE2D_FRAMEWORK}/src)
    target_link_libraries(aimgui_live2d
        PUBLIC  Framework Live2DCubismCore
        PRIVATE vulkan android log jnigraphics aaudio)

    target_compile_definitions(AImGui PRIVATE AIMGUI_LIVE2D)
    target_link_libraries(AImGui PRIVATE aimgui_live2d vulkan)
    # aimgui_core holds renderer_vk/bloom_vk/window_session, which under Live2D
    # force the Vulkan backend and link the *real* libvulkan (prototypes on) so
    # they interoperate with Cubism's Vulkan renderer instead of the dlsym
    # wrapper. Propagate the mode to everything that links aimgui_core.
    target_compile_definitions(aimgui_core PUBLIC AIMGUI_LIVE2D AIMGUI_REAL_VULKAN)
    target_link_libraries(aimgui_core PUBLIC vulkan)
    message(STATUS "AImGui: Live2D Cubism support ENABLED (Vulkan)")
endif()
