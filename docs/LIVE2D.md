# Live2D Cubism support (optional)

AImGui can render a **Live2D Cubism** model behind the ImGui UI. It's **off by
default** and requires the proprietary **Cubism Core** runtime, which Live2D
does not allow to be redistributed — so you supply the SDK yourself.

> Status: **first-draft integration.** The build wiring is complete, but the C++
> glue (`jni/src/live2d/`) has not yet been compiled/run against a real SDK in
> this repo — expect to iterate on-device. It renders one model with
> breathing/eye-blink/physics; motions & expressions are loaded by the SDK but
> not yet driven.

## 1. Get the SDK (Core + Framework)

Download the **Cubism 5 SDK for Native** (agree to Live2D's license):
<https://www.live2d.com/en/sdk/download/native/>

Unzip and copy two folders from the *same* package (versions must match):

| From SDK      | To repo                               |
| ------------- | ------------------------------------- |
| `Core/`       | `third_party/Core/`                   |
| `Framework/`  | `third_party/CubismNativeFramework/`  |

After copying you should have:

```
third_party/Core/include/Live2DCubismCore.h
third_party/Core/lib/android/arm64-v8a/libLive2DCubismCore.a
third_party/CubismNativeFramework/CMakeLists.txt
```

Both paths are `.gitignore`d — nothing proprietary is committed.

## 2. Add a model

Put a Cubism 3+ model under `live2d/models/`, e.g. `live2d/models/Hiyori/…`.
Sample models ship in the SDK (`Samples/Resources/`). See
[`live2d/models/README.md`](../live2d/models/README.md). Mind each model's
license.

## 3. Build

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26d
./build.sh -DAIMGUI_LIVE2D=ON        # auto-bumps to android-30 (AImageDecoder)
# → libs/arm64-v8a/AImGui
```

## 4. Push model + run

```bash
adb push live2d/models/Hiyori /data/local/tmp/live2d/Hiyori
adb push libs/arm64-v8a/AImGui /data/local/tmp/AImGui
adb shell chmod +x /data/local/tmp/AImGui
adb shell su -c /data/local/tmp/AImGui
```

On start the app scans `/data/local/tmp/live2d/` for the first `*.model3.json`
and loads it. Watch `adb logcat -s AImGui_Live2D AImGui_Cubism` for load status.

## Notes / known limitations

- **GL backend only.** With `AIMGUI_LIVE2D=ON` the app forces the OpenGL ES
  renderer (Cubism ships a GLES renderer); the Vulkan path is bypassed.
- **Compositing with bloom** is untuned — the model is drawn into the scene
  framebuffer before ImGui, so the post-process bloom will also affect it. If
  that looks wrong, draw order / a separate pass is the thing to adjust
  (`main.cpp` render loop + `jni/src/core/renderer_gl.cpp`).
- **Motions/expressions** are loaded but not yet triggered; wire
  `StartMotion`/expression selection into `Model::Update` next.
- Textures are decoded with `AImageDecoder` (API 30+), hence the platform bump.

## Where the code lives

```
CMakeLists.txt                 # AIMGUI_LIVE2D option + Framework/Core wiring
jni/include/live2d/live2d_view.h
jni/src/live2d/live2d_view.cpp   # framework startup, auto-load, projection, draw
jni/src/live2d/live2d_model.*    # CubismUserModel subclass (load/update/draw)
jni/src/live2d/live2d_texture.*  # PNG → GL texture (AImageDecoder)
jni/src/live2d/live2d_allocator.h
jni/src/main.cpp                 # #ifdef AIMGUI_LIVE2D render-loop hooks
```
