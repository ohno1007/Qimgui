cmake_minimum_required(VERSION 3.18)
project(QimguiMD3 VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

# This directory is a standalone entry point and is also consumed by the
# Android application. Nothing here selects a platform or renderer backend.
get_filename_component(QIMGUI_MD3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
option(QIMGUI_MD3_BUILD_SHOWCASE "Build the optional MD3 component galleries" ON)
option(QIMGUI_MD3_BUILD_ANDROID_OVERLAY "Build the optional Android/root process host" OFF)
option(QIMGUI_MD3_INSTALL "Generate install rules for the component library" ON)

set(_md3_imgui "${QIMGUI_MD3_ROOT}/third_party/imgui")
add_library(qimgui_md3_imgui STATIC
    "${_md3_imgui}/imgui.cpp"
    "${_md3_imgui}/imgui_draw.cpp"
    "${_md3_imgui}/imgui_tables.cpp"
    "${_md3_imgui}/imgui_widgets.cpp")
add_library(Qimgui::ImGui ALIAS qimgui_md3_imgui)
set_target_properties(qimgui_md3_imgui PROPERTIES
    EXPORT_NAME ImGui POSITION_INDEPENDENT_CODE ON)
target_compile_features(qimgui_md3_imgui PUBLIC cxx_std_17)
target_include_directories(qimgui_md3_imgui PUBLIC
    $<BUILD_INTERFACE:${_md3_imgui}>
    $<BUILD_INTERFACE:${QIMGUI_MD3_ROOT}/config>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qimgui/third_party/imgui>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qimgui/config>)
target_compile_definitions(qimgui_md3_imgui PUBLIC IMGUI_USER_CONFIG="aimgui_imconfig.h")

file(GLOB _md3_sources CONFIGURE_DEPENDS "${QIMGUI_MD3_ROOT}/jni/src/ui/md3/*.cpp")
set(_md3_gallery_sources ${_md3_sources})
list(FILTER _md3_gallery_sources INCLUDE REGEX "_(gallery|showcase)\\.cpp$")
list(FILTER _md3_sources EXCLUDE REGEX "_(gallery|showcase)\\.cpp$")
add_library(aimgui_md3 STATIC ${_md3_sources})
add_library(Qimgui::MD3 ALIAS aimgui_md3)
set_target_properties(aimgui_md3 PROPERTIES EXPORT_NAME MD3 POSITION_INDEPENDENT_CODE ON)
target_compile_features(aimgui_md3 PUBLIC cxx_std_17)
target_include_directories(aimgui_md3 PUBLIC
    $<BUILD_INTERFACE:${QIMGUI_MD3_ROOT}/jni/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aimgui_md3 PUBLIC Qimgui::ImGui)

set(_md3_export_targets qimgui_md3_imgui aimgui_md3)
if(QIMGUI_MD3_BUILD_SHOWCASE)
    add_library(aimgui_md3_showcase STATIC ${_md3_gallery_sources})
    add_library(Qimgui::MD3Showcase ALIAS aimgui_md3_showcase)
    set_target_properties(aimgui_md3_showcase PROPERTIES
        EXPORT_NAME MD3Showcase POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(aimgui_md3_showcase PUBLIC Qimgui::MD3)
    list(APPEND _md3_export_targets aimgui_md3_showcase)
endif()

if(QIMGUI_MD3_BUILD_ANDROID_OVERLAY)
    add_subdirectory("${QIMGUI_MD3_ROOT}/libraries/android_overlay" android-overlay)
    list(APPEND _md3_export_targets qimgui_android_overlay)
endif()

if(QIMGUI_MD3_INSTALL)
    include(CMakePackageConfigHelpers)
    install(TARGETS ${_md3_export_targets} EXPORT QimguiMD3Targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    file(GLOB _md3_headers CONFIGURE_DEPENDS "${QIMGUI_MD3_ROOT}/jni/include/ui/md3*.h")
    list(FILTER _md3_headers EXCLUDE REGEX "/md3_demo\\.h$")
    install(FILES ${_md3_headers} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ui")
    install(DIRECTORY "${QIMGUI_MD3_ROOT}/jni/include/qimgui/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qimgui" FILES_MATCHING PATTERN "*.hpp"
        PATTERN "android_overlay.hpp" EXCLUDE)
    install(FILES "${_md3_imgui}/imgui.h" "${_md3_imgui}/imconfig.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qimgui/third_party/imgui")
    install(FILES "${QIMGUI_MD3_ROOT}/config/aimgui_imconfig.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qimgui/config")
    install(FILES "${QIMGUI_MD3_ROOT}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3")
    install(FILES "${_md3_imgui}/LICENSE.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3" RENAME ImGui-LICENSE.txt)
    install(FILES "${QIMGUI_MD3_ROOT}/third_party/material_shapes/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3" RENAME MaterialShapes-LICENSE.txt)
    install(FILES "${QIMGUI_MD3_ROOT}/third_party/material_shapes/NOTICE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3" RENAME MaterialShapes-NOTICE.txt)
    install(DIRECTORY "${QIMGUI_MD3_ROOT}/third_party/material_expressive/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/QimguiMD3/material_expressive"
        FILES_MATCHING PATTERN "LICENSE" PATTERN "NOTICE" PATTERN "provenance.json" PATTERN "README.md")
    set(_md3_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/QimguiMD3")
    configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/QimguiMD3Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/QimguiMD3Config.cmake"
        INSTALL_DESTINATION "${_md3_config_dir}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QimguiMD3ConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion)
    install(EXPORT QimguiMD3Targets NAMESPACE Qimgui:: DESTINATION "${_md3_config_dir}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QimguiMD3Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QimguiMD3ConfigVersion.cmake"
        DESTINATION "${_md3_config_dir}")
endif()
