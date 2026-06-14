# AImGui

A minimal, elegant **Dear ImGui** framework that builds to a single Android
ARM64 ELF executable — no JNI, no APK, no Activity. Runs as a native binary on
top of SurfaceFlinger.

- ImGui **v1.92.8**
- **Vulkan + OpenGL ES 3** backends with automatic VK→GL fallback
- **System CJK font** auto-detected (`NotoSansCJK`, `DroidSansFallback`, …)
- Post-process bloom on both backends
- Volume-key driven Dynamic Island collapse
- Layered into three independently-updatable static libraries
- No `imgui_demo`, no debug tools, no FreeType, no FontAwesome
- Size-optimized (`-Os`, `--gc-sections`, `--icf=all`, strip)

Stripped ELF for `arm64-v8a`: **~720 KB**.

## Layout

```
CMakeLists.txt                       single build entry; replaces the old jni/Android.mk tree
build.sh                             configures NDK toolchain + invokes cmake
jni/
├── include/
│   ├── imgui/                       public ImGui headers (vendored)
│   └── ui/
│       ├── ui.h                     UiState struct + DrawUi() + ripple::TouchLastItem()
│       └── main_ui.h                Page enum + dispatcher
└── src/
    ├── main.cpp                     entry point, render loop, permeate-record toggle
    │
    ├── ui/
    │   ├── ui.cpp                   window framework: dynamic island, shatter, ripple, resize grip
    │   └── main_ui.cpp              per-page bodies (Dashboard / Widgets / Window / Performance / About)
    │
    ├── platform/                    ── libaimgui_platform.a (vendored glue) ──
    │   ├── ANativeWindowCreator.h   SurfaceFlinger window via direct symbol walk (MIT)
    │   └── TouchHelperA.{h,cpp}, Utils.h, VectorStruct.h, spinlock.h
    │
    └── core/                        ── libaimgui_core.a (project-owned) ──
        ├── renderer.h               IRenderer interface + MakeRenderer()
        ├── renderer_factory.cpp     VK→GL fallback
        ├── renderer_gl.cpp          EGL + OpenGL ES 3
        ├── renderer_vk.cpp          Vulkan (Android Surface)
        ├── vulkan_wrapper.{h,cpp}   dynamic loader (no -lvulkan needed)
        ├── bloom_gl.cpp             GL post-process bloom (luma threshold → 2-pass blur → composite)
        ├── bloom_vk.cpp             Vulkan equivalent
        ├── font.{h,cpp}             system CJK font loader
        ├── frame_pacer.h            drift-corrected sleep-until pacer
        ├── keyboard_input.{h,cpp}   volume key polling
        └── window_session.{h,cpp}   RAII for the (ANativeWindow, IRenderer) pair
```

## Backend selection

By default `aimgui::MakeRenderer(Backend::Auto)` tries Vulkan first and falls
back to OpenGL ES 3. Force either backend explicitly:

```cpp
auto r = aimgui::MakeRenderer(window, W, H, aimgui::Backend::OpenGL); // or Vulkan
```

## CJK font

`aimgui::LoadDefaultAndSystemCJKFont(size_px)` adds the bundled ProggyClean
default and merges the system font found at:

```
/system/fonts/NotoSansCJK-Regular.ttc
/system/fonts/NotoSerifCJK-Regular.ttc
/system/fonts/NotoSansCJKsc-Regular.otf
/system/fonts/DroidSansFallbackFull.ttf
/system/fonts/DroidSansFallback.ttf
/system/fonts/NotoSansSC-Regular.otf
```

Glyphs are rasterized lazily, so the atlas stays small even with the full
0x4E00–0x9FFF CJK Unified Ideographs range enabled.

## Build

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26d   # r25+
./build.sh
# → libs/arm64-v8a/AImGui
```

## Run

The binary creates a SurfaceFlinger overlay and reads `/dev/input/*`, so it
needs elevated privileges (root, or a shell with the right capabilities):

```bash
adb push libs/arm64-v8a/AImGui /data/local/tmp/AImGui
adb shell chmod +x /data/local/tmp/AImGui
adb shell su -c /data/local/tmp/AImGui
```

A draggable, Chinese-capable ImGui window appears over the current display.

## Customize

UI content lives in `jni/src/ui/main_ui.cpp`. Add a new page:

1. Append to `enum class Page` and to `kPages` in `jni/include/ui/main_ui.h`.
2. Write a `DrawYourPage(state)` body in `main_ui.cpp`.
3. Add the `case` in `DrawPage()`.

The window framework (sidebar, dynamic island, resize, shatter exit, ripple)
lives in `ui.cpp` and shouldn't need touching for normal content changes.

## Known issues & gotchas

A log of traps hit during the CMake migration + on-device testing. Keep these
in mind before changing the relevant areas — most of them aren't obvious from
the code.

### `Touch::Init` wants real height, not `W` twice

`main.cpp` derives `W = max(width, height)` for the square surface, then
calls `Touch::Init({W, H}, ...)` to give the helper its mapping basis.
Passing `{W, W}` here looks innocuous but scales finger Y by the wrong
factor — taps land off-target and the window can't be dragged or
clicked. Keep the explicit `H = min(width, height)`. (Fixed in `eca022b`.)

### Don't enable real LTO

The original `Application.mk` listed `-flto` under `APP_LDFLAGS` only.
Without a matching compile-side flag no LTO IR landed in the objects, so
link-time LTO was a no-op. The first CMake port put `-flto` into
`add_compile_options` too — i.e. real LTO for the first time — which
appears to interact badly with the heavy-static-state SurfaceFlinger
helper paths. Keep `-flto` off both sides (the ELF is still ~720 KB,
well under budget). (Restored in `7ad8b98`.)

### Tear down `WindowSession` before `ImGui::DestroyContext()`

The renderer backend's `Shutdown()` reaches into the active ImGui
context to unhook itself. If `ImGui::DestroyContext()` runs first
(e.g. because you let `WindowSession`'s destructor handle teardown on
stack unwind, after `DestroyContext()` at the bottom of `main`), the
backend dereferences a dead context. `main` calls `ws.Destroy()`
explicitly before `ImGui::DestroyContext()`.

### Screen recording is a four-way knot

The window is a SurfaceFlinger overlay created directly via
`SurfaceComposerClient`, not a normal app window. That means its
visibility / inclusion in screen recordings is driven by two independent
SurfaceFlinger flags:

| Flag                            | Effect                                                                      |
| ------------------------------- | --------------------------------------------------------------------------- |
| `eSkipScreenshot` (windowFlags) | Layer is hidden from screenshot APIs.                                       |
| `SetTrustedOverlay(true)`       | Layer is excluded from MediaProjection captures *and* from input dispatch.  |

`SetTrustedOverlay(true)` does double duty — without it, MediaProjection
recorders can capture our layer (good!), but our full-screen overlay
sits in the input dispatch tree without an input channel and eats every
touch (bad — taps to apps below stop working). So `TrustedOverlay` has
to stay on, and the only way to make the surface appear in recordings
is the mirror trick: `SurfaceComposerClient::mirrorSurface()` re-parents
a copy onto the recorder's VirtualDisplay layerStack.

That works only when the `mirrorSurface` symbol resolves — and its
**signature changed across versions**, so its mangled name did too:

```
Android 11-13:  _ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlE
                → mirrorSurface(SurfaceControl*)
Android 14-16:  _ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_
                → mirrorSurface(SurfaceControl*, SurfaceControl* parent)
```

The helper resolves **both** ABIs (a `MirrorSurface` 1-arg and a
`MirrorSurface2` 2-arg function pointer) and calls whichever the ROM
exports, passing `parent = nullptr` for the 2-arg form. Binding the
2-arg overload to a 1-arg prototype reads a garbage parent pointer and
segfaults the instant the recorder's VirtualDisplay appears, so the
prototype has to match the number of parameters exactly.

If the hard-coded names miss (non-AOSP ROM), the helper **scans
`libgui.so`'s dynamic symbol table** for the
`SurfaceComposerClient::mirrorSurface` method token and binds the match
*by ABI* — the mangled tail (`...EPNS_14SurfaceControlE` vs
`...EPNS_14SurfaceControlES2_`) decides which function pointer it fills,
so a wrong-arity overload is never bound. See `EnumerateDynSyms()` /
`FindDynSymContaining()` in `jni/src/platform/ANativeWindowCreator.h`.

The call still null-guards before jumping, so on the (rare) ROM where no
matching symbol exists at all the app degrades gracefully — no crash when
the system recorder spins up its VirtualDisplay, the surface just won't
be captured. To inspect what a device actually exports:

```bash
adb pull /system/lib64/libgui.so
nm -D --demangle libgui.so | grep -i mirror
```

Current state on this repo:

- ✅ Screen recording no longer crashes the app.
- ✅ Taps pass through to apps below the overlay.
- ✅ `防录屏 = ON` correctly hides the window from recordings (via `eSkipScreenshot`).
- ✅ `防录屏 = OFF` makes the window visible in recordings on Android 14+/16 via the 2-arg `mirrorSurface(SurfaceControl*, parent)` overload (verified on Android 16).

### `ProcessMirrorDisplay()` runs from the render loop

The helper fork+execs `dumpsys display`, parses the result, and posts
SurfaceFlinger transactions. It internally throttles to 1 Hz so the
per-frame `if (!st.permeate_record) ProcessMirrorDisplay();` is cheap.
Moving it to a dedicated background thread was tried (`6a84b55`) to dodge
what looked like a transaction race; that turned out to be misdiagnosed
— the real culprit was the null `mirrorSurface` function pointer. Don't
bother re-threading it; the main-loop call is fine.

## Credits

- [Dear ImGui](https://github.com/ocornut/imgui) — Omar Cornut
- [AndroidSurfaceImgui-Enhanced](https://github.com/AFan4724/AndroidSurfaceImgui-Enhanced) — window + touch glue (MIT)
- Vulkan wrapper from Google's NDK samples (Apache-2.0)
