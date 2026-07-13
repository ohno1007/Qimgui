# AImGui — APK (non-root)

A **root-free Android APK** port of the native AImGui ELF. Same Dear ImGui UI,
same GL/Vulkan renderers, same CJK font loader and bloom — but instead of
punching a SurfaceFlinger overlay and reading `/dev/input/*` (both root-only),
it runs inside an ordinary `android.app.NativeActivity`:

| Concern            | ELF build (root)                              | APK build (this folder)                     |
| ------------------ | --------------------------------------------- | ------------------------------------------- |
| Window             | `ANativeWindowCreator` → SurfaceFlinger layer | `NativeActivity` Surface (`app->window`)    |
| Touch input        | reads `/dev/input/event*`                     | `AInputEvent` motion from the framework     |
| Volume-key toggle  | reads `/dev/input/event*`                     | `AInputEvent` key (`AKEYCODE_VOLUME_*`)     |
| Privileges         | needs `su`                                    | **none** — zero `uses-permission` entries   |
| Anti-record toggle | SurfaceFlinger trusted-overlay / mirror       | no-op (not possible for a normal window)    |

Everything below the window/input boundary is **reused verbatim** from the
repo's `../jni` tree — the renderers (`renderer_gl.cpp`, `renderer_vk.cpp`,
`vulkan_wrapper.cpp`), bloom, `font.cpp`, and the entire UI (`ui.cpp`,
`main_ui.cpp`). Only the root-dependent glue (`ANativeWindowCreator.h`,
`TouchHelperA.cpp`, `keyboard_input.cpp`, `window_session.cpp`) is dropped and
replaced by the ~250-line `src/main/cpp/android_main.cpp`.

## Layout

```
app/
├── build.gradle                 AGP module (arm64-v8a, minSdk 24, com.aimgui.app)
├── settings.gradle
├── gradle.properties
├── CMakeLists.txt               reuses ../jni sources + NDK native_app_glue
└── src/main/
    ├── AndroidManifest.xml       NativeActivity, no permissions, fullscreen
    ├── cpp/android_main.cpp      NativeActivity entry point (replaces jni/src/main.cpp)
    └── res/                      launcher icon + strings
```

## Build

Requires the Android SDK (platform-34, build-tools 34), NDK `26.3.11579264`,
and CMake `3.22.1`. Point `local.properties` (or `$ANDROID_HOME`) at the SDK.

```bash
cd app
echo "sdk.dir=/path/to/android-sdk" > local.properties

# debug APK (auto-signed with the debug key)
gradle :assembleDebug
#   → build/outputs/apk/debug/AImGui-debug.apk

# release APK (stripped; also signed with the debug key for easy install)
gradle :assembleRelease
#   → build/outputs/apk/release/AImGui-release.apk   (~0.95 MB)
```

## Live2D character (optional)

The APK can also embed the **Live2D Cubism** floating character — the same layer
the ELF build enables with `-DAIMGUI_LIVE2D=ON`, ported to the NativeActivity
render loop. It's **off by default** (a fresh clone builds the lean APK with no
proprietary deps). To build it in:

1. Drop the Cubism SDK into `../third_party` (`Core/` + `CubismNativeFramework/`)
   and a model into `../live2d/models/<Model>/` — see [docs/LIVE2D.md](../docs/LIVE2D.md).
2. Put `glslc` (NDK `shader-tools/`) on `PATH` — it compiles Cubism's Vulkan shaders.
3. Build with the flag:

```bash
export PATH="$ANDROID_HOME/ndk/26.3.11579264/shader-tools/linux-x86_64:$PATH"
gradle :assembleRelease -Paimgui.live2d=true
```

The model + compiled SPIR-V are embedded into `libaimgui.so`, so the APK is
self-contained (no `adb push` of model files). The Live2D build **forces the
Vulkan backend** (Cubism's renderer is Vulkan) and raises `minSdk` to 30
(textures are decoded with `AImageDecoder`). It boots as the draggable
character; tap it / drag it, and it expands into the window.

## Install & run

```bash
adb install -r build/outputs/apk/release/AImGui-release.apk
# then tap the "AImGui" launcher icon — no root, no adb shell su
```

A fullscreen Dear ImGui surface appears with the Dashboard / Widgets / Window /
Performance / About pages. Touch drives the pointer; **Volume Up/Down** collapse
and expand the Dynamic Island, exactly like the ELF build.

## Notes

- **arm64-v8a only**, matching the vendored `prebuilt/arm64-v8a/libimgui.a` and
  the ELF build. Add other ABIs once matching imgui prebuilts exist.
- The Vulkan backend loads through the dlsym `vulkan_wrapper` (no `-lvulkan`),
  and falls back to OpenGL ES 3 automatically, same as the ELF build.
- The **anti-screen-recording** toggle in the UI is inert here: it depends on
  SurfaceFlinger trusted-overlay / mirror tricks that only a system/root
  process can perform. The switch still renders but does nothing.
- Live2D is not wired into the APK build (the ELF `AIMGUI_LIVE2D` path is
  untouched and still builds via the root `CMakeLists.txt`).
