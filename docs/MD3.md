# 原生 MD3 组件库

`aimgui_md3` 使用 Dear ImGui 输入与绘制列表，没有 WebView、Java 或 DEX 运行依赖。`AImGui` 保留左侧导航、右侧内容的布局，外壳、导航、五个页面与弹窗均使用这套组件；顶部一个“液体玻璃”开关只切换背景效果。主界面、独立 Android 程序 `MD3Demo` 与组件截图程序复用同一份 `Showcase()`。

`./build.sh` 同时生成两个程序。[MD3_MAIN.md](MD3_MAIN.md) 说明主界面的导航、设置与玻璃切换验证。运行 `MD3Demo` 可独立检查八类组件及可重置的交互样例，具体操作与预期结果见 [MD3_DEMO.md](MD3_DEMO.md)。演示窗口代码位于 `jni/src/demo/md3_demo.cpp`，不依赖 Android，可用于主机交互回归与截图。

## 使用

```cpp
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_extra.h"

// 在 ImGui context 创建后调用。
aimgui::md3::SetTheme(false, 1.0f);
// 可选：SetFonts(body_font, medium_font)，对应 400 / 500 字重。
aimgui::md3::PushTheme();
ImGui::Begin("Example");
static bool enabled = true;
aimgui::md3::Switch("Enabled", &enabled);
if (aimgui::md3::Button("Save")) { /* 保存 */ }
ImGui::End();
aimgui::md3::PopTheme();
```

推荐使用统一入口 `<qimgui/md3.hpp>` 并链接 `Qimgui::MD3`。`libraries/md3/` 可以独立构建、安装，并通过 `find_package(QimguiMD3)` 接入其他工程；展示样例单独链接 `Qimgui::MD3Showcase`。旧头文件与 `aimgui_md3` target 仍可使用。分层、构建和接入示例见 [框架文档](MD3_FRAMEWORK.md)。

Android MD3 使用与对照截图相同的 Roboto 400／500。CMake 通过 `scripts/embed_md3_fonts.py` 校验并嵌入 `tests/md3_web/fonts/` 的固定 TTF 字节，不需要 npm 或浏览器参与 Android 构建。正文和标签均采用 `2400 / 2048` 的字体尺寸修正，与截图程序一致；中文由设备系统字体补充，不覆盖拉丁字形。`PushTheme` 同时设置 14dp 正文字体，`PopTheme` 恢复调用方字体。

尺寸为逻辑 dp，按 `Theme::scale` 换算。显示比例同时缩放字体、控件与间距，不是独立的字号设置。`GetTheme()` 可修改颜色。基础控件使用 `BeginDisabled/EndDisabled`；部分组合控件另有 enabled 参数。标签支持 `文字##唯一ID`。

图标参数支持 `md3_icons.h` 的语义标记，例如 `@home`、`@widgets`、`@settings`、`@speed` 与 `@info`。这些轮廓矢量图形使用 24 单位网格，无需图标字体；它们并非官方 Material Symbols 字形的精确复刻。未知标记仍使用文字／字形回退。

`md3_shapes.h` 提供 AndroidX `MaterialShapes` 的全部 35 种预设形状，使用固定版本官方算法导出的三阶贝塞尔曲线，支持填充、描边、旋转和缩放。主界面与独立测试窗口的“形状”分类复用同一份展示；接口、来源和再生步骤见 [MD3_SHAPES.md](MD3_SHAPES.md)。

`BeginCard` 无论返回值如何都必须 `EndCard`。菜单、对话框和面板只在 Begin 返回 true 后调用 End。对话框／面板的 open 状态由调用方持有。日期范围先选起点，再选终点；倒序会整理为升序。

## 组件覆盖

| 接口文件 | 组件 |
| --- | --- |
| `md3.h` | 五种按钮、四种图标按钮、三种 FAB／扩展 FAB、复选框／混合态、单选框、开关、四种 Chip、连续／步进／范围滑块、Filled／Outlined 输入框 |
| `md3_layout.h` | 三种卡片、一／二／三行列表、分隔线、徽标、主／次标签栏、单／多选分段按钮、导航条／侧栏／抽屉、四种顶部应用栏、两种进度条、Snackbar、Tooltip、基本／全屏对话框、底部／侧边面板 |
| `md3_selection.h` | 下拉选择、搜索建议、菜单／子菜单、日期、日期范围、12／24 小时时间选择器 |
| `md3_extra.h` | 图片／文字轮播、底部应用栏、富提示 |
| `md3_shapes.h` | 35 种 MaterialShapes 预设、原始三阶曲线读取、填充／描边绘制、旋转和尺寸适配 |
| `md3_toolbar.h`、`md3_split_button.h` | 横／竖工具栏、可切换工具项、主动作与菜单独立的分割按钮 |
| `md3_button_group.h`、`md3_progress.h` | 单／多选连接按钮组、按压扩张、线性／圆形波浪进度 |
| `md3_motion.h` | 每 context 的动画时长比例与减少动态效果设置 |

新增组件的交互约定和示例见 [Expressive 文档](MD3_EXPRESSIVE.md)。主窗口与 `MD3Demo` 的“表现力组件”分类可直接试用。

## 交互约定

- 离散滑块的 `steps` 表示区间数：方向键移动一个刻度，非刻度值向操作方向进入最近刻度；Home/End 到边界。范围滑块两端重合后仍能向左右拖开。
- 标签栏支持左右方向键循环切换、Home/End 跳到首末可用标签，并跳过禁用项。
- 时间表盘可连续拖动；分钟支持 0–59 的每一分钟，24 小时模式通过内外圈选择。选定小时并松手后进入分钟；方向键按小时或分钟调整。
- Snackbar 在鼠标悬停或键盘聚焦操作按钮时暂停倒计时，移开后继续。调用方仍持有显示与剩余时间状态。

## 验证和边界

交互：`tests/run_md3_checks.sh`，含基础控件、时间/日期选择与布局反馈组件的 ASan/UBSan 回归。Expressive 运行 `tests/run_md3_expressive_checks.sh`，形状几何与绘制运行 `tests/run_md3_shapes_checks.sh`，独立 SDK 消费运行 `tests/run_md3_package_checks.sh`。窗口集成分别运行 `tests/run_md3_main_checks.sh`、`tests/run_md3_demo_checks.sh` 与 `tests/run_md3_clock_checks.sh`，配置运行 `tests/run_config_checks.sh`。按层选择更多验证入口见 [代码结构](ARCHITECTURE.md#按改动选择验证)。

原生组件渲染：`tests/render_md3.py`；含玻璃材质的主界面预览：`tests/render_md3_main.py`。官方网页参考与固定依赖：`tests/md3_web/`。源版本、令牌和对照范围见 [MD3_REFERENCE.md](MD3_REFERENCE.md)。

截图检查控件尺寸、形状、颜色、文字和状态。浏览器与 ImGui 的文字栅格化、阴影和动画存在差异，截图数量不代表像素级一致。Material Web 未提供的组件在参考文档中单独注明。

- `AImGui` 与 `MD3Demo` 共用 MD3 组件库，分别构建和启动；独立测试窗口不读写主程序配置，也不初始化玻璃效果。
- 移除 DEX 后不自动唤起 Android 系统软键盘；evdev 支持外接键盘。
- 未接入 TalkBack 语义树，也未从壁纸生成动态色。
- 主界面和独立测试窗口均使用固定 Roboto 与系统中文回退，中文外观依设备字体而异。
- Android 真机触摸、旋转、输入与 GLES／Vulkan 一致性需要设备验证。
