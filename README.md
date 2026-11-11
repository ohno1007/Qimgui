# Qimgui

基于 **Dear ImGui 1.93.0** 的 C++17 Material Design 3 组件库，包含可独立安装的 SDK、Android 窗口宿主和应用示例。
组件由 ImGui 原生绘制，包含常规 MD3 控件、四类 Expressive 组件和 35 种 AndroidX MaterialShapes；无需 WebView、Java 或 DEX。

## 选择入口

| 需求 | 入口 | 构建／说明 |
| --- | --- | --- |
| 在已有 ImGui 应用中使用组件 | `<qimgui/md3.hpp>`、`Qimgui::MD3` | [框架接入](docs/MD3_FRAMEWORK.md)；独立 CMake 入口 `libraries/md3/` |
| 查看全部组件和交互样例 | `MD3Demo` | `./build.sh`；[组件测试窗口](docs/MD3_DEMO.md) |
| 查看左右布局和可选液体玻璃 | `AImGui` | `./build.sh`；[主界面](docs/MD3_MAIN.md) |
| 从 SDK 编写新的 Android 应用 | `examples/md3_clock/` → `MD3Clock / Tempo` | [先安装 SDK，再编译示例](docs/MD3_CLOCK.md) |
| 验证无窗口的外部工程接入 | `examples/md3_consumer/` | [独立消费样例](docs/MD3_FRAMEWORK.md#可运行消费样例与验证) |
| 不要用户态进程，整套跑在内核 | `kernel/` → `qimgui.ko` | [内核态移植](docs/KERNEL_PORT.md)；需目标内核工具链 |

`AImGui` 使用 Vulkan，并在初始化失败时回退 GLES 3；支持主题、字体回退、触感反馈、设置持久化及可选 Live2D。`MD3Demo` 和 `MD3Clock` 共用可选 `Qimgui::AndroidOverlay` 宿主，以 GLES 3 提交帧。Android 程序通过 SurfaceFlinger 创建窗口，无需 APK 或 Activity。

这是持续绘制、由 VSync 控制提交节奏的界面，**尚未实现静止时完全停止绘制**。

## 构建

已验证的工具链：Android NDK **r29 / 29.0.14206865**、CMake 3.18+。

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r29
./build.sh
# 输出：libs/arm64-v8a/AImGui 和 libs/arm64-v8a/MD3Demo
```

根目录是 Android 应用工程，需要 Python 3 生成固定字体资源；仅使用组件时可直接构建 `libraries/md3/`，无需 Android 工具链。`MD3Clock` 的独立工程始终使用已安装 SDK，不由 `build.sh` 构建。

ImGui 源码已随仓库提供，普通构建不需要下载依赖。默认关闭 Live2D，相关 SDK 和模型按
[Live2D 文档](docs/LIVE2D.md)准备。开启时执行 `./build.sh -DAIMGUI_LIVE2D=ON`。

## 运行

程序创建系统级 Surface 并读取触摸设备，需要 root 或等价的系统权限。
部署本次构建的 ELF：

```bash
adb push libs/arm64-v8a/AImGui /data/local/tmp/AImGui
adb shell chmod 755 /data/local/tmp/AImGui
adb shell su -c /data/local/tmp/AImGui
```

音量键可收起／展开窗口。`AImGui` 包含概览、组件、设置、性能和关于五页；“组件”页提供交互样例与完整组件分类。主界面操作与验证见 [MD3 主界面](docs/MD3_MAIN.md)。独立组件测试程序另行启动：

```bash
adb push libs/arm64-v8a/MD3Demo /data/local/tmp/MD3Demo
adb shell chmod 755 /data/local/tmp/MD3Demo
adb shell su -c /data/local/tmp/MD3Demo
```

先退出另一个测试程序，避免两个进程同时读取触摸设备。`MD3Demo` 支持拖动、调整窗口大小、显示比例、明暗主题和交互状态重置。
该独立程序由共享 AndroidOverlay 宿主处理 Surface、字体、输入、GLES 3 和录屏镜像；组件页面不启动玻璃背景采集、辉光或传感器，也不读取／保存主程序配置。
测试步骤见 [MD3 测试样例](docs/MD3_DEMO.md)。

独立 SDK 消费示例 `MD3Clock` 的构建、运行和源码结构见 [Tempo 示例](docs/MD3_CLOCK.md)。

已移除 Java 输入法与系统剪贴板桥接，
文字输入通过原生键盘事件完成；目前需要外接键盘，不会自动唤起 Android 软键盘。
MD3 控件支持 ImGui 的键盘导航，按 Tab 切换焦点。

录屏请选择“整个屏幕”，启动后等待约 3 秒，检查 `[recording]` 日志。录制 `AImGui` 时还需关闭顶部“液体玻璃”和设置页“防录屏”。Android 17 录屏兼容性仍待新版真机视频确认，已知接口适配与验证范围见 [Android 接入记录](docs/ANDROID_NOTES.md)。

## 项目结构

| 路径 | 职责 |
| --- | --- |
| `libraries/md3/` | 可移植组件、可选图库和 SDK 安装规则 |
| `jni/include/qimgui/`、`jni/include/ui/md3*.h` | 统一 SDK 入口和组件公共 API |
| `jni/src/ui/md3/` | 组件实现与可选图库 |
| `libraries/android_overlay/`、`jni/src/runtime/` | 共享 Android 宿主和基础 GLES 渲染器 |
| `jni/src/demo/` | `MD3Demo` 页面及薄入口，复用共享宿主 |
| `examples/` | 安装包消费示例和独立的 Tempo 应用 |
| `jni/src/main.cpp`、`jni/src/ui/` | `AImGui` 生命周期、主窗口和页面 |
| `jni/src/platform/`、`jni/src/core/` | 平台适配、输入、字体、GL／VK 后端和玻璃等应用能力 |
| `third_party/`、`config/` | 固定上游源码、许可、来源记录和统一 ImGui 配置 |
| `scripts/`、`tests/` | 资源生成、来源校验、行为回归和软件预览 |
| `kernel/` | 内核模块：软件光栅器、DRM plane 接入、input handler 和内核侧 C++ 运行时 |
| `docs/`、`release/` | 使用文档、开发说明和构建产物获取说明 |

目录边界、依赖方向、开发位置和测试选择见 [代码结构](docs/ARCHITECTURE.md)。
[Android 接入记录](docs/ANDROID_NOTES.md)解释了旋转、窗口销毁顺序与录屏兼容性等历史问题。

MD3 接入与组件清单见 [使用文档](docs/MD3.md)，网页参考与截图复现见 [参考说明](docs/MD3_REFERENCE.md)。
组件库可通过 `<qimgui/md3.hpp>` 与 `Qimgui::MD3` 独立接入，支持 CMake 安装和 `find_package`，详见 [框架接入](docs/MD3_FRAMEWORK.md)。工具栏、分割按钮、连接按钮组和波浪进度位于“表现力组件”分类，详见 [Expressive 组件](docs/MD3_EXPRESSIVE.md)。
MaterialShapes 支持填充、描边、旋转和缩放，在“组件 → 完整组件 → 形状”或独立测试窗口的“形状”分类查看；接口与官方曲线再生见 [形状文档](docs/MD3_SHAPES.md)。
主界面采用 24 单位网格的语义轮廓矢量图标，不依赖图标字体；这些图形并非官方 Material Symbols 字形的精确复刻。

## 内核态

`kernel/` 把绘制、输入和显示整套放进内核模块：没有用户态进程，不创建 Android Surface，
界面由内核 CPU 光栅化后经 DRM plane 交给 SDE 与 Android 画面硬件混合。

应用层源码不变：`AImGui` 的五个页面（`jni/src/ui/`）、MD3 组件（`jni/src/ui/md3/`）和 ImGui
按原样编入模块，设备中文字体由内核 `filp_open` 读入，设置由内核自己读写。

内核态版本去掉了液体玻璃、辉光、Vulkan／GLES、Live2D 和触感反馈。
窗口可以被肉眼看到，但系统截图和录屏抓不到它。主机验证用 `tests/run_kernel_checks.sh`，
真机相关部分（DRM 接入、触摸拦截、NEON 区间延迟）尚未验证，范围见 [内核态移植](docs/KERNEL_PORT.md)。

## 液体玻璃

主界面只保留顶部“液体玻璃”效果开关。关闭时显示 MD3 实色表面，开启且屏幕采集可用时显示玻璃背景；导航、按钮、表单和主题保持 MD3。采集不可用、正在启动或失败时继续显示可操作的实色界面，并显示状态。

GLES 与 Vulkan 共用 `glass_common.glsl`，减少两套材质实现发生偏差的机会。

- 由距离场合成圆角与连接区域，按像素导数平滑边缘。
- 中央区域保持清晰，折射和有限色散集中在边缘。
- 一次 9 点模糊加边缘色散／反射，最重分支最多 12 次纹理读取；旧实现最多 44 次。
  这是着色器读取次数对比，**不代表真机帧率提高相同比例**。
- 统一上原点坐标；屏幕纹理和 OpenGL framebuffer 副本分别处理采样方向。
- 区分硬连接和柔性连接，避免同一面板的接缝鼓包。

修改材质后重新生成两套后端资源：

```bash
scripts/compile_glass_shaders.sh
scripts/compile_glass_shaders.sh --check
```

使用 NDK r29 的 `glslc` 保持生成结果可复现。生成的头文件随仓库提交，正常构建不需要编译着色器。

![软件渲染的玻璃材质对比](docs/images/glass-comparison.png)

图中使用相同的测试背景、坐标和材质参数，展示着色器变化；不是 Android 真机截图。

## 升级 ImGui

当前来源：[`v1.93.0`](https://github.com/ocornut/imgui/releases/tag/v1.93.0)，
提交 `c1aaac65e896a45f81f47a4d68f9959e0f86c859`。

```bash
python3 scripts/update_imgui.py v1.93.0
python3 scripts/verify_sources.py
./build.sh
```

更新脚本同步核心、头文件、GL／VK 后端和来源清单。`prepare-imgui-update` 工作流会生成
经过构建检查的补丁供审阅，不会强制覆盖远端分支。

1.93 适配包含 `CmdLists.Size`、Vulkan sampled-image／sampler 描述符池，以及
文字描边对图集中圆角和线条区域的排除。不要只修改版本宏。

## 验证

```bash
tests/run_host_checks.sh
tests/run_md3_checks.sh
tests/run_md3_shapes_checks.sh
tests/run_md3_expressive_checks.sh
tests/run_md3_package_checks.sh
tests/run_md3_demo_checks.sh
tests/run_md3_main_checks.sh
tests/run_md3_clock_checks.sh
tests/run_overlay_runtime_checks.sh
tests/run_config_checks.sh
python3 -m pip install -r tests/requirements.txt
python3 tests/render_glass.py --output build/glass-preview.png
```

主机检查使用 ASan／UBSan，覆盖图集形状、文字描边、纹理延迟上传、回调、索引与原生键盘事件。
MD3 检查覆盖控件输入、禁用状态、日期边界、嵌套弹层、涟漪、窄窗口及多主题／多上下文。SDK 检查覆盖可选目标、独立消费和安装目录移动后的接入；Tempo 和宿主测试分别验证页面状态与 Android 循环中的录屏调用。
主界面检查覆盖五页导航、主题与显示大小、单一玻璃开关、交互状态、弹窗、窗口操作及退出；配置检查覆盖新旧配置读取、保存与无效值处理。
GLES 检查使用 Mesa／EGL 渲染实际着色器，验证坐标、边缘覆盖、framebuffer 方向与屏幕边界。
主界面截图复现命令见 [MD3_MAIN.md](docs/MD3_MAIN.md)：实际 ImGui 绘制数据与生产 GLES 玻璃着色器合成在测试背景上，不是 Android 真机截图。

CI 构建 ARM64 ELF，并校验上游源码与生成着色器。Android 真机的触摸、
屏幕镜像、外接键盘、旋转、GL／VK 视觉一致性和功耗仍需要设备验证；Live2D 需要单独提供 SDK。
按改动范围选择平台 ABI、生命周期和来源校验的入口，见 [验证分组](docs/ARCHITECTURE.md#按改动选择验证)。

## 致谢与许可

- Qimgui：Apache-2.0，见 [LICENSE](LICENSE)。
- [Dear ImGui](https://github.com/ocornut/imgui)：Omar Cornut，MIT；许可证随上游源码保留。
- [Material Web](https://github.com/material-components/material-web)：Apache-2.0；用于 MD3 官方源码参考和独立网页截图。
- [AndroidX MaterialShapes](https://developer.android.com/reference/kotlin/androidx/compose/material3/MaterialShapes)：Android Open Source Project，Apache-2.0；用于 35 种原生预设的官方曲线数据，来源与许可见 [形状文档](docs/MD3_SHAPES.md)。
- Roboto：OFL-1.1；用于原生 MD3 和网页对照，许可证见 [OFL.txt](tests/md3_web/fonts/OFL.txt)。
- [AndroidSurfaceImgui-Enhanced](https://github.com/AFan4724/AndroidSurfaceImgui-Enhanced)：窗口／触摸接入来源。
- Vulkan wrapper：Google NDK samples，Apache-2.0。
- Live2D Cubism 的 SDK 和模型遵循其各自许可，不包含在本仓库中。
