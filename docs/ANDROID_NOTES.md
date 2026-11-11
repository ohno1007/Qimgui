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
helper paths. Keep `-flto` off both sides until the private interface paths have
been validated with it on devices. (Restored in `7ad8b98`.)

### Tear down `WindowSession` before `ImGui::DestroyContext()`

The renderer backend's `Shutdown()` reaches into the active ImGui
context to unhook itself. If `ImGui::DestroyContext()` runs first
(e.g. because you let `WindowSession`'s destructor handle teardown on
stack unwind, after `DestroyContext()` at the bottom of `main`), the
backend dereferences a dead context. `main` explicitly calls `ws.Destroy()`
before ending the process. Once touch workers have started, the current
hosts release input and use `std::_Exit` rather than destroy a context
still referenced by those workers. Initialization-failure paths that have
not started input can destroy the context normally.

### 录屏显示镜像与玻璃背景采集

直接创建的 SurfaceFlinger 窗口与普通 Activity 窗口不同：截图可见不代表系统录屏一定包含该图层。录屏显示可能使用独立 layer stack；因此既要允许采集本窗口，也要在受支持的系统上向录屏显示创建窗口镜像。

| 接口／机制 | 当前职责 |
| --- | --- |
| `eSkipScreenshot` | 控制窗口的采集排除标记；用于防录屏和避免实时玻璃自反馈 |
| `SetTrustedOverlay(true)` | 保留现有输入转发所需的 overlay 配置；不通过修改它绕开录屏问题 |
| `ANativeWindowCreator::ProcessMirrorDisplay()` | 发现外部录屏显示，将本窗口镜像到对应 layer stack |
| `ScreenMirror` | 为实时玻璃获取背景帧；与录屏显示镜像是两条不同路径 |

`AImGui` 在关闭防录屏时调用录屏适配；`Qimgui::AndroidOverlay` 宿主持续调用它，使 `MD3Demo` 和 `MD3Clock` 共用显示发现与清理行为。实时玻璃需要排除自己的图层以避免反馈，录制主窗口时应同时关闭玻璃与防录屏。普通 SDK 示例不启动玻璃背景采集。

`mirrorSurface` 按完整导出符号绑定到独立函数指针：

| 已知签名 | 可选参数 |
| --- | --- |
| `mirrorSurface(SurfaceControl*)` | 无 |
| `mirrorSurface(SurfaceControl*, SurfaceControl* stopAt)` | `stopAt = nullptr` |
| `mirrorSurface(SurfaceControl*, SurfaceControl* stopAt, SurfaceControl* cropBy)` | 两者均为 `nullptr` |

解析以实际签名为准，不只按 Android 版本选择，也不把同名未知重载强行转换。接口缺失时记录 `[recording]` 日志并保留正常窗口。Android 17 的三参数来源与设备证据见后文。需要检查设备导出时可提取 `libgui.so` 后使用 `llvm-nm -D --demangle`，不要由部分名称推断参数布局。

历史 Android 16 设备曾确认双参数录屏镜像、触摸转发与防录屏切换；这些结果不能证明 Android 17 或其他 ROM 的录屏兼容性。目前 Android 17 的窗口启动已有反馈，保存视频中的镜像结果仍待新版真机确认。

### `ProcessMirrorDisplay()` 从帧循环调用

适配函数内部每 3 秒执行一次 `dumpsys display`，解析显示信息并更新 Surface 事务；其余帧调用会立即返回。`AImGui` 与共享 `RunOverlay` 在开始 ImGui 帧之前调用它。隐藏窗口时宿主仍轮询，使停止／重启录屏与显示变化能够被发现；渲染器提交透明内容以同步隐藏状态。

录屏断开、显示标识复用、分辨率变化及窗口销毁由同一镜像缓存管理。`ANativeWindowCreator::Destroy()` 清理镜像和根层，再销毁主窗口。早期曾尝试把调用移到后台线程，后来确认当时故障来自空函数指针；没有因此引入独立镜像线程。

### 启动即崩溃的诊断构建

`-DAIMGUI_DIAGNOSTICS=ON` 保留 DWARF 调试符号、帧指针和 unwind 数据，并启用 Surface 接口解析日志。`main` 向终端输出 `[boot]` 阶段；显示尺寸查询失败会直接退出。诊断构建使用独立目录，以便保留与设备文件对应的未剥离 ELF。

```bash
cmake -S . -B build/diagnostic-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DAIMGUI_DIAGNOSTICS=ON
cmake --build build/diagnostic-arm64 -j5
```

将该 ELF 命名为 `AImGui`，与 `scripts/run_android_diagnostics.sh` 一起放到设备可执行目录，再用 root 执行脚本。它会创建独立的 `diagnostics-*` 目录，记录设备版本、启动输出、退出码，以及按本次进程 PID 筛选的应用日志；保留原进程退出码。

Android 17 尚未完成真机兼容性验证。私有 SurfaceFlinger 接口即使符号名相同，也不能据此确认 ABI 兼容；仍需结合设备的启动日志或 native tombstone 定位。


### Android 17 的 `createSurface` 元数据参数

AOSP 提交 [`a5756cdc7747253081338df64c81e122950fbaa3`](https://github.com/LineageOS/android_frameworks_native/commit/a5756cdc7747253081338df64c81e122950fbaa3) 将 `createSurface` 的 `LayerMetadata` 参数从按值传递改为 `const LayerMetadata&`，其余参数顺序不变。上述链接为保留上游提交历史的源码镜像。

Android 14+ 路径先查找旧版完整符号；缺失时再查找新版完整符号，使用独立的 const-reference 函数指针调用。选择依据是实际导出的签名，支持厂商回移该变更的系统。诊断版调用前会输出 `[surface] createSurface ABI: const LayerMetadata&` 或旧版 value ABI。

若两种已知签名都不存在，只打印设备导出的同名候选用于诊断并退出，不调用未经确认的重载。故障模拟测试覆盖两种已知接口的参数转发和未知签名拒绝；实际 Android 17 运行仍需设备验证。


### 首帧后传感器事件导致栈越界

`SensorTilt::Update` 原先使用手写的 88 字节事件结构，NDK 的 `ASensorEvent` 为 104 字节，包含 `flags` 和 `reserved1` 尾部。系统读取满批 16 个事件时会向 1408 字节缓冲区写入 1664 字节，造成栈越界。现在直接使用 NDK 的事件类型和函数声明。`tests/run_sensor_checks.sh` 使用真实 NDK 头文件验证批量事件、元素步长、队列排空和失败清理；旧实现可在同一测试中复现 ASan 错误。

同时给设备提供的多点触摸 slot 增加索引边界检查；不支持的 slot 不再访问 `Finger[10]` 之外的内存。对应回归为 `tests/run_touch_checks.sh`。

诊断构建出现致命信号时会输出 `[crash]`，包含信号、PC/LR、可执行文件加载基址，以及可用时的 ELF 相对地址。`main_stage` 表示主线程最近的步骤，不能单凭它认定工作线程的崩溃位置。格式化阶段不分配内存或展开栈，之后恢复原信号处理器，保留 Android debuggerd 的 tombstone 处理。用与设备完全相同的 ELF 对相对地址运行 `llvm-addr2line -C -f -e AImGui <offset>` 定位。

### Android 17 屏幕镜像接口的重载与参数变化

AOSP 提交 [`1a9bfbc4`](https://github.com/LineageOS/android_frameworks_native/commit/1a9bfbc4f4f1d8930bdeababac7ab48bce58d3ae) 为 `Transaction::setDisplaySurface` 增加了接收 `sp<Surface>` 的重载。原有代码按函数名称片段查找，可能将 `IGraphicBufferProducer` 传给这个新重载，造成错误的虚函数调用。现在仅解析接收 `sp<IGraphicBufferProducer>` 的完整符号，并检查实际 `status_t` 返回值。

[`b6765609`](https://github.com/LineageOS/android_frameworks_native/commit/b6765609ddc085893a8942320b69b9779f719779) 为 `createVirtualDisplay` 增加了 `ownerUid` 参数。4、5、6 参数版本现在分别使用完整符号和对应函数指针；6 参数版本显式传入默认无效 UID。镜像路径中的其他动态接口也改为完整签名查找，未知重载不会被尝试调用。已知接口不可用时报告缺失项；启动失败会清理虚拟显示与 reader，5 秒后再尝试，不修改保存的开关设置。

`tests/run_mirror_checks.sh` 验证重载选择、参数转发、未知签名拒绝和状态返回；`tests/run_screen_mirror_checks.sh` 覆盖启动失败后的清理。上述源码链接为保留 AOSP 历史的镜像。主机测试与交叉编译不能替代设备验证，之前仅有 `main_stage=screen-mirror` 的日志不足以证明具体是哪一个接口崩溃。

诊断版现在细分镜像创建、绑定、取帧及 GPU 导入阶段；`[crash]` 增加 `tid/main_tid`、`pc_module/pc_module_offset` 和可用时的 LR 模块信息。库地址表在正常执行阶段缓存，信号处理器只读取缓存，不调用 `dladdr` 或展开栈。

用户已确认 v4 在本次 Android 17 / SDK 37、1216×2640、root 环境正常运行，首帧后的崩溃不再出现。该反馈只覆盖这台设备和本次运行，不代表所有 Android 17 ROM；随后新增的独立 `MD3Demo` 窗口需要单独真机测试。

### 关闭防录屏与玻璃后，录屏缺少悬浮窗

用户后续反馈：Android 17 能开始录屏，但在“防录屏”和“液体玻璃”均关闭时，保存的视频仍缺少悬浮窗。没有该次录屏期间的设备显示信息，不能仅凭此现象确认 ROM 上的具体原因。

检查发现，录屏路径使用的镜像缓存与清理函数访问的缓存不是同一个对象，且镜像层及其根层被额外保留到进程结束；窗口重建或录屏显示复用 layer stack 时可能沿用旧镜像。另外，旧显示信息解析会跨越下一项查找缺失字段，并对格式变化直接调用抛异常的整数转换。现将解析限定在各项内部，校验字段；镜像与根层由实际缓存共同持有，清理、显示移除及显示标识变化时释放并重建。应用自身的玻璃捕获 stack 不作为外部录屏目标。

录屏路径增加 `[recording]` 日志，区分接口缺失、镜像创建失败及事务失败。设置截图标志时检查事务结果，只有成功才记住已应用状态；停止玻璃采集不会再清除用户明确开启的防录屏设置。原有 Android 17 Surface 创建与镜像 ABI 修复继续保留，没有通过关闭 trusted overlay 来改变输入行为。

回归入口为 `tests/run_display_dump_checks.sh` 和 `tests/run_recording_mirror_checks.sh`。真机验证应选择“整个屏幕”，保持玻璃与防录屏均关闭，检查首次录制、停止后重新录制，以及防录屏开关切换后的视频。实时液体玻璃仍需要全局排除本窗口以避免自反馈，因此玻璃开启时不承诺录屏包含该窗口；本次没有加入静态背景录制方案。

### Android 17 录屏的三参数 `mirrorSurface`

用户进一步确认截图包含窗口，而视频没有。新版日志显示开始录屏后 `parsed displays=2, external targets=1, mirrorSurface=unavailable`，随后重复报告 `mirrorSurface unavailable on Android 17`。这确认了录屏显示已经被发现，当前失败点是没有解析到受支持的镜像接口，而非显示检测没有启动。

Android 17 源码快照 [`a96adf2c6e62dbb22299cd83bbb9ea4bc569e985`](https://github.com/LineageOS/android_frameworks_native/blob/a96adf2c6e62dbb22299cd83bbb9ea4bc569e985/libs/gui/include/gui/SurfaceComposerClient.h) 声明了非静态成员 `mirrorSurface(SurfaceControl* source, SurfaceControl* stopAt, SurfaceControl* cropBy)`，比此前支持的版本增加了第三个指针参数。对应[实现](https://github.com/LineageOS/android_frameworks_native/blob/a96adf2c6e62dbb22299cd83bbb9ea4bc569e985/libs/gui/SurfaceComposerClient.cpp)允许 `stopAt`、`cropBy` 均为 null；链接为保留上游历史的源码镜像。

适配按完整导出符号绑定独立的三参数函数指针，传入当前 composer、窗口 SurfaceControl 和两个 null，保留原有一／二参数版本。未识别的重载不调用。诊断日志会在解析成功时显示 `mirrorSurface=3-arg`，创建成功后显示 `mirrored window to stack ...`。主机回归验证参数转发和未知重载拒绝，录屏画面仍需更新后的设备运行确认。


### SDK 宿主遗漏录屏轮询

`MD3Clock` 的 Android 17 日志已显示成功提交首帧并以状态码 0 正常退出，但用户反馈视频没有悬浮窗，且没有 `[recording]` 日志。检查发现首次拆分的 `RunOverlay` 未调用主程序已有的 `ProcessMirrorDisplay()`：窗口允许截图，录屏显示却从未被检测。

共享宿主现已在 `NewFrame()` 前加入该调用，启动输出 `[overlay] recording mirror enabled`。`MD3Demo` 也改为消费同一宿主，避免重复入口再次漏掉平台能力。宿主回归执行真实帧循环和 ImGui，模拟录屏晚于应用启动、隐藏时发现、停止／重启及退出；移除调用的负向对照会失败。平台镜像 ABI 和缓存清理由独立测试覆盖。

回归入口为 `tests/run_overlay_runtime_checks.sh` 与 `tests/run_recording_mirror_checks.sh`。真机请选择“整个屏幕”并等待至少一个约 3 秒的轮询周期，保留 `[recording] parsed displays=...` 和 `mirrored window to stack ...` 或失败日志。该修正已经通过主机验证和交叉编译，仍需检查实际保存的视频。
