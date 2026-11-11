# Material 3 Expressive 组件

本次新增图片中明确列出的四类组件：工具栏、分割按钮、波浪进度指示器和连接按钮组。它们属于同一套原生 MD3 库，使用当前主题、字体、缩放和动画偏好。主窗口入口为“组件 → 完整组件 → 表现力组件”；`MD3Demo` 的对应分类复用相同展示代码。

统一包含 `<qimgui/md3.hpp>` 并链接 `Qimgui::MD3`。框架结构、独立构建和安装方式见 [MD3_FRAMEWORK.md](MD3_FRAMEWORK.md)，可运行的消费代码见 [`examples/md3_consumer/main.cpp`](../examples/md3_consumer/main.cpp)。

## 组件与状态

| 组件 | 公共 API | 调用方状态与返回值 |
| --- | --- | --- |
| 工具栏 | [`Toolbar`](../jni/include/ui/md3_toolbar.h) | `ToolbarItem` 可绑定 `bool* selected`；返回本帧激活项索引，否则为 -1。支持横向／纵向、Standard／Vibrant |
| 分割按钮 | [`SplitButton`](../jni/include/ui/md3_split_button.h) | `bool* expanded` 由调用方持有；分别返回 `Primary`、`ToggleMenu` 或 `None` |
| 连接按钮组 | [`ButtonGroup`、`MultiButtonGroup`](../jni/include/ui/md3_button_group.h) | 单选使用 `int*`，多选使用 `uint64_t*` 位集合；返回选择是否改变 |
| 波浪进度 | [`WavyLinearProgress`、`WavyCircularProgress`](../jni/include/ui/md3_progress.h) | 调用方传入进度值；只显示状态，不处理输入 |

尺寸为逻辑 dp，按 `Theme::scale` 换算。工具栏默认容器厚度为 64dp、操作区域为 48dp；小型分割按钮与按钮组默认高度为 40dp。工具栏和按钮组接受 1–64 项。窄空间内会压缩布局并裁剪文字，应用应根据可用宽度控制项目数量。

`BeginDisabled/EndDisabled` 对交互组件生效；工具栏和按钮组还支持每项禁用。工具栏沿排列方向用箭头键移动焦点，Home/End 跳到端点，跳过禁用项；分割按钮用左右键在两个操作间移动焦点。Enter/Space 激活焦点项。单选按钮组的左右键和 Home/End 改变选择；多选组的方向键仅移动焦点，Enter/Space 切换对应位。多选 `allowEmpty=false` 时保留最后一个选中项。

`ButtonGroupOptions` 可设置总宽、高度、间距、`expandedRatio`、样式和动画。按住一项时该项变宽、相邻项收缩；松开后恢复。选中项使用圆形端点，内部连接项采用小圆角。

## 分割按钮与菜单配对

分割按钮报告操作，不持有菜单内容。菜单关闭后应同步 `expanded`，这样点击外部和 Escape 关闭也能恢复箭头状态。下例只累计本地计数：

```cpp
#include <qimgui/md3.hpp>
namespace md = qimgui::md3;

// 在已有 ImGui 窗口内、每帧调用；同一实例使用稳定且唯一的 ID。
static bool expanded = false;
static int actions = 0;
expanded = ImGui::IsPopupOpen("share-actions");
const auto result = md::SplitButton("share", "Share", &expanded,
                                    md::ButtonStyle::Tonal, "@share");
if (result == md::SplitButtonAction::Primary) ++actions;
if (result == md::SplitButtonAction::ToggleMenu && expanded)
    md::OpenMenu("share-actions");
if (md::BeginMenuPopup("share-actions")) {
    if (!expanded) ImGui::CloseCurrentPopup();
    if (md::MenuItem("Copy link")) ++actions;
    if (md::MenuItem("Save a copy")) ++actions;
    md::EndMenuPopup();
}
expanded = ImGui::IsPopupOpen("share-actions");
```

主操作不会自动改变展开状态。菜单项的实际行为由应用实现。不同分割按钮可以用 `PushID/PopID` 复用上述内部 ID。

## 进度与动画

进度值 `[0,1]` 表示确定进度，大于 1 的有限值按完成处理；负数或非有限值（含 NaN、Infinity）表示不定进度。确定进度值直接反映调用方输入，不额外插值。

`WavyProgressStyle` 提供：

- `amplitude`：默认波幅的 0–1 倍，线性默认峰值 3dp，圆形为 1.6dp；确定进度在 ≤10% 和 ≥95% 区间使用直线／平滑圆弧。
- `wavelength`：波长，0 使用默认值；线性确定／不定模式分别为 40／20dp，圆形为 15dp。
- `strokeWidth`、`trackWidth`、`gap`：活动线、背景轨道的粗细及二者间隔，默认均为 4dp。
- `stopSize`：线性终点圆点大小，默认 4dp，0 隐藏。
- `waveSpeed`：每秒波形周期，0 冻结波形；`animate=false` 同时停止不定进度运动。

线性进度 `width=0` 使用可用内容宽度；圆形 `diameter` 是完整占位尺寸。粗线条会增加所需空间。控件通过主色与 secondary container 颜色绘制，不需要纹理或额外渲染后端。

[`MotionSettings`](../jni/include/ui/md3_motion.h) 按 ImGui context 保存。`SetMotionSettings({true, 1.f})` 减少动态效果，`durationScale=0` 也立即完成状态过渡并令不定进度保持稳定显示。默认比例 1 为正常速度，2 表示两倍时长；有限值限制在 0–10，非有限比例恢复为 1。更换主题和缩放会保留这些设置。

## 来源与覆盖范围

四类组件参考 AndroidX 官方实现与设计 token，固定提交为 `587b8da057e3dfa7fb908e4b62b1bb84271eb20d`。原始参考文件、SHA-256、Apache-2.0 许可与来源记录位于 [`third_party/material_expressive/`](../third_party/material_expressive/) 的 `toolbar_split`、`button_group` 和 `progress` 子目录。

这些是适配 ImGui 的原生组件和动画，实现不依赖 Compose，动画不承诺与 Compose 逐帧一致。此次覆盖上述四类组件；图片中“全部 14 个新和更新的组件”所代表的完整 Expressive 套件尚未全部实现。

## 验证与预览

```bash
tests/run_md3_expressive_checks.sh
build/md3-expressive-checks/md3-expressive-test --capture build/md3-expressive-captures \
  tests/md3_web/fonts/Roboto-400.ttf tests/md3_web/fonts/Roboto-500.ttf
python3 tests/render_md3.py build/md3-expressive-captures --output build/md3-expressive-preview
tests/run_md3_package_checks.sh
```

测试检查参考源码哈希，并通过 ASan/UBSan 覆盖鼠标／键盘交互、禁用状态、分割按钮菜单、按钮组选择与几何、进度及动画参数、多个 context 和窄布局。输出 `md3-expressive-light.png`、`md3-expressive-dark.png` 使用实际原生绘制数据经 Mesa 渲染，属于主机预览；Android 设备显示与输入仍需真机验证。
