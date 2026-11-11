# Qimgui MD3 组件框架

组件实现、展示样例和 Android 运行环境现在分别构建。应用通过统一头文件 `<qimgui/md3.hpp>` 和 CMake target `Qimgui::MD3` 接入；现有 `ui/md3*.h`、`aimgui::md3` 和 `aimgui_md3` 保留兼容。

库使用调用方当前的 ImGui context，产生 ImGui 绘制列表。它不创建窗口、接管输入、启动 Android Surface，也不连接 OpenGL／Vulkan 后端。应用保留事件循环、窗口、纹理上传和渲染器的所有权。

## 结构

| 层 | 入口／target | 职责 |
| --- | --- | --- |
| 基础与主题 | `md3.h`、`md3_motion.h` | 每 context 的颜色、缩放、字体、动画偏好；状态由调用方持有 |
| 常规组件 | `md3.h`、`md3_layout.h`、`md3_selection.h`、`md3_extra.h` | 按钮、表单、导航、容器、选择器、菜单、反馈等 |
| Expressive 组件 | `md3_toolbar.h`、`md3_split_button.h`、`md3_button_group.h`、`md3_progress.h` | 工具栏、分割按钮、连接按钮组、波浪进度 |
| 形状与图标 | `md3_shapes.h`、`md3_icons.h` | 35 种官方预设曲线和语义矢量图标 |
| 库实现 | `Qimgui::MD3` | 上述可复用组件，不包含图库或应用运行时 |
| ImGui 核心 | `Qimgui::ImGui` | 固定版本的四个核心源文件与统一编译配置 |
| 可选展示 | `<qimgui/md3_showcase.hpp>`、`Qimgui::MD3Showcase` | `Showcase()`、布局／选择器图库 |
| 可选 Android 宿主 | `<qimgui/android_overlay.hpp>`、`Qimgui::AndroidOverlay` | Android/root 进程窗口、GLES3、字体、输入及录屏显示镜像 |
| 应用 | 根目录 Android 工程、`examples/md3_clock/` | `AImGui` 完整界面、`MD3Demo` 组件窗口和使用已安装 SDK 的 Tempo |

`libraries/md3/` 是可单独配置的 CMake 入口。源文件继续位于 `jni/src/ui/md3/`，公共头位于 `jni/include/`；核心 target 排除 `*_gallery.cpp` 与 `*_showcase.cpp`，展示代码单独进入可选库。组件库本身不要求 Android NDK、Python、Java、npm、图形驱动或字体下载。

仓库层次、target 依赖和按改动选择测试的入口见 [代码结构](ARCHITECTURE.md)。

## 在现有工程中接入

```cmake
set(QIMGUI_MD3_BUILD_SHOWCASE OFF)
set(QIMGUI_MD3_INSTALL OFF)
add_subdirectory(/path/to/Qimgui/libraries/md3 qimgui-md3)
target_link_libraries(your_app PRIVATE Qimgui::MD3)
```

应用自己的 ImGui 渲染／输入后端应链接同一份 `Qimgui::ImGui`。不要再把另一份 `imgui.cpp` 或 ImGui 静态库链接进进程。库使用内部 ImGui API，因此版本和 `IMGUI_USER_CONFIG` 必须一致；CMake target 会传递固定的头文件路径、C++17 要求和配置宏。升级 ImGui 时应一起更新来源记录并运行回归测试。

在已有 ImGui context 和帧循环中使用：

```cpp
#include <qimgui/md3.hpp>
namespace md = qimgui::md3; // 等同于 aimgui::md3

// context 创建后设置。可按需加载自己的字体并调用 SetFonts(body, medium)。
md::SetTheme(false, 1.f);
md::SetMotionSettings({false, 1.f});

// 每帧，在应用的 ImGui::NewFrame() 与 ImGui::Render() 之间：
{
    md::ScopedTheme theme; // PushTheme / PopTheme 的 RAII 配对
    if (ImGui::Begin("Editor")) {
        static bool bold = true, italic = false;
        const md::ToolbarItem tools[] = {
            {"bold", "B", nullptr, &bold},
            {"italic", "I", nullptr, &italic},
        };
        md::Toolbar("format", tools, 2);

        static int period = 0;
        const md::ButtonGroupItem periods[] = {{"Day"}, {"Week"}, {"Month"}};
        md::ButtonGroup("period", &period, periods, 3);
        md::WavyLinearProgress("upload", .4f);
        md::WavyCircularProgress("pending", -1.f);
    }
    ImGui::End();
}
```

`ScopedTheme` 只管理 ImGui 样式与字体栈，不创建或销毁 context。它必须在同一存活 context 内构造、析构，且不能跨越帧或 context 的生命周期。主题设置、组件状态、纹理和字体所有权不交给这个作用域。

## 统一交互约定

- 控件 ID 支持 ImGui 的 ID 栈及 `文字##唯一ID`；显式 `id` 参数与可见文字分离。循环中可使用 `ImGui::PushID`。
- 尺寸默认使用逻辑 dp，经过 `Theme::scale` 换算。`DrawShape` 等底层绘图接口使用物理像素，具体以头文件注释为准。
- 主题、字体和动画偏好按 ImGui context 隔离。`GetTheme()` 可以修改调色板；`SetTheme()` 切换标准浅色／深色和缩放，不改动动画偏好。
- `SetMotionSettings({true, 1.f})` 启用减少动画；`durationScale=0` 同样立即完成状态过渡，并令不定进度保持静态。正常速度为 1，数值 2 表示两倍时长。
- 单选、多选、进度、开关和菜单展开状态由应用保存。组件通过返回值报告本帧操作，应用决定保存、导航或执行其他动作。
- `BeginDisabled/EndDisabled` 对交互组件统一生效；部分组合组件还提供单项禁用。工具栏、分割按钮和按钮组均支持键盘导航，具体方向键规则见各自头文件。
- `SplitButton` 将主操作和展开菜单作为两个独立返回值。应用负责菜单内容，并在点击外部或 Escape 关闭菜单后同步 `expanded`。完整配对用法见消费样例。

## 单独构建、安装和分发

```bash
cmake -S libraries/md3 -B build/md3-library \
  -DCMAKE_BUILD_TYPE=Release -DQIMGUI_MD3_BUILD_SHOWCASE=ON
cmake --build build/md3-library --parallel
cmake --install build/md3-library --prefix /path/to/md3-sdk
```

安装内容包含静态库、公共头文件、固定 ImGui 配置、CMake package 配置，以及 Qimgui／ImGui／MaterialShapes／Expressive 来源与许可。生成的包可以移动到其他目录。它对应构建时的平台和架构，不能将主机库链接进 Android；Android 库用相同入口加 NDK toolchain、ABI 和 API level 参数重新构建。

消费安装包时：

```cmake
find_package(QimguiMD3 0.1 REQUIRED CONFIG COMPONENTS MD3)
target_link_libraries(your_app PRIVATE Qimgui::MD3)

# 需要可选图库时，安装时必须启用 QIMGUI_MD3_BUILD_SHOWCASE：
# find_package(QimguiMD3 0.1 REQUIRED CONFIG COMPONENTS MD3 Showcase)
# target_link_libraries(your_app PRIVATE Qimgui::MD3Showcase)
```

通过 `CMAKE_PREFIX_PATH=/path/to/md3-sdk` 或 `QimguiMD3_DIR` 指定安装位置。当前 package/API 版本为 `0.1.0`；这不是对不同 ImGui 版本的二进制兼容承诺。

## 可选 Android 运行时与应用示例

构建 SDK 时可启用 `QIMGUI_MD3_BUILD_ANDROID_OVERLAY=ON`，生成独立的 `Qimgui::AndroidOverlay` target。该选项默认关闭，开启时要求 Android NDK toolchain；`Qimgui::MD3` 本身继续保持可移植，不依赖这个宿主。

根 Android 工程为 `MD3Demo` 启用同一宿主，入口只提供页面回调。宿主和基础 GLES 渲染器集中在 `jni/src/runtime/`，录屏轮询、显示更新和输入释放不会在不同示例中各自维护。

[`examples/md3_clock/`](../examples/md3_clock/) 是使用安装后 SDK 的完整应用示例：左侧 Navigation Rail，右侧闹钟、时钟、计时器和秒表页面。入口、窗口布局、页面和业务状态分别编写，CMake 只通过 `find_package(QimguiMD3 CONFIG COMPONENTS MD3 AndroidOverlay)` 接入组件与运行时。

`RunOverlay` 拥有 ImGui context、渲染和输入循环，应用在回调中组合组件。它是从 `main()` 调用一次的进程宿主；现有触摸后端在启动后保留工作线程，因此关闭时释放输入后使用 `std::_Exit` 结束进程。持久化应在回调返回 `false` 前完成。NDK 消费外部 SDK 建议直接设置 `QimguiMD3_DIR`，避免默认包搜索规则屏蔽安装目录。构建命令和完整生命周期说明见 [MD3Clock](MD3_CLOCK.md)。

## 可运行消费样例与验证

[`examples/md3_consumer/`](../examples/md3_consumer/) 是独立的外部 CMake 工程。它仅包含公共入口，组合四种 Expressive 组件和形状，创建 ImGui context 并生成三帧绘制数据：

```bash
cmake -S examples/md3_consumer -B build/md3-consumer
cmake --build build/md3-consumer --parallel
build/md3-consumer/md3_consumer

tests/run_md3_package_checks.sh
```

消费样例为无窗口的集成检查，终端报告绘制顶点数；它没有 GPU 后端，不用于展示截图。交互展示请运行 Android `MD3Demo` 或主窗口组件页。可将样例中的 `DrawExample()` 放进已有应用的帧循环。

`run_md3_package_checks.sh` 检查源码子目录消费、独立构建、可选图库、安装目录移动后的 `find_package` 消费，并检查组件静态库未引用 Android Surface／传感器／ImGui 渲染后端。组件行为、形状几何和窗口交互继续由各自的 ASan/UBSan 测试覆盖。
