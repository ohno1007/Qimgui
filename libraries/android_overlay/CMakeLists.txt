# Optional process host for Android consumers of the installed MD3 package.
# Component UI stays in Qimgui::MD3; this target links only the ImGui core.
if(NOT ANDROID)
    message(FATAL_ERROR "QIMGUI_MD3_BUILD_ANDROID_OVERLAY requires an Android NDK toolchain")
endif()
if(NOT TARGET Qimgui::ImGui)
    message(FATAL_ERROR "Add libraries/md3 first; AndroidOverlay shares its pinned ImGui core")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_overlay_jni "${QIMGUI_MD3_ROOT}/jni")
set(_overlay_fonts "${QIMGUI_MD3_ROOT}/tests/md3_web/fonts")
set(_overlay_font_header "${CMAKE_CURRENT_BINARY_DIR}/generated/md3_fonts.h")
add_custom_command(
    OUTPUT "${_overlay_font_header}"
    COMMAND "${Python3_EXECUTABLE}" "${QIMGUI_MD3_ROOT}/scripts/embed_md3_fonts.py"
        --font-dir "${_overlay_fonts}" --output "${_overlay_font_header}"
    DEPENDS "${QIMGUI_MD3_ROOT}/scripts/embed_md3_fonts.py"
        "${_overlay_fonts}/Roboto-400.ttf" "${_overlay_fonts}/Roboto-500.ttf"
    COMMENT "Embedding Android overlay Roboto fonts"
    VERBATIM)

add_library(qimgui_android_overlay STATIC
    "${_overlay_font_header}"
    "${_overlay_jni}/src/runtime/android_overlay.cpp"
    "${_overlay_jni}/src/runtime/simple_renderer.cpp"
    "${_overlay_jni}/src/platform/TouchHelperA.cpp"
    "${_overlay_jni}/src/core/font.cpp"
    "${_overlay_jni}/src/core/keyboard_input.cpp"
    "${_overlay_jni}/src/core/crash_diagnostics.cpp"
    "${QIMGUI_MD3_ROOT}/third_party/imgui/backends/imgui_impl_opengl3.cpp")
add_library(Qimgui::AndroidOverlay ALIAS qimgui_android_overlay)
set_target_properties(qimgui_android_overlay PROPERTIES
    EXPORT_NAME AndroidOverlay POSITION_INDEPENDENT_CODE ON)
target_compile_features(qimgui_android_overlay PUBLIC cxx_std_17)
target_include_directories(qimgui_android_overlay
    PUBLIC
        $<BUILD_INTERFACE:${_overlay_jni}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${_overlay_jni}/src"
        "${QIMGUI_MD3_ROOT}/third_party/imgui/backends"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(qimgui_android_overlay PRIVATE IMGUI_IMPL_OPENGL_ES3)
# The root Android application owns its diagnostic policy. Standalone SDK
# builds retain diagnostics by default, as before, without redefining the
# application's SURFACE_LOG_ENABLE=0 in ordinary release builds.
if(NOT DEFINED AIMGUI_DIAGNOSTICS OR AIMGUI_DIAGNOSTICS)
    target_compile_definitions(qimgui_android_overlay PRIVATE
        SURFACE_LOG_ENABLE=1 AIMGUI_DIAGNOSTICS=1)
endif()
target_compile_options(qimgui_android_overlay PRIVATE
    -ffunction-sections -fdata-sections -fno-omit-frame-pointer -funwind-tables)
target_link_libraries(qimgui_android_overlay PUBLIC
    Qimgui::ImGui log android EGL GLESv3 dl)

if(QIMGUI_MD3_INSTALL)
    install(FILES "${_overlay_jni}/include/qimgui/android_overlay.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qimgui")
    install(FILES "${_overlay_fonts}/OFL.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3"
        RENAME Roboto-OFL.txt)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/NOTICE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3"
        RENAME AndroidOverlay-NOTICE.txt)
endif()
