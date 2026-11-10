# Android 平台接入记录

以下保留历史排查记录；具体设备结论不代表所有 ROM 已通过验证。

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
