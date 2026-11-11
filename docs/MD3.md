# 原生 MD3 组件库

`aimgui_md3` 使用 Dear ImGui 输入与绘制列表，没有 WebView、Java 或 DEX 运行依赖。Android“控件”页与截图程序调用同一份 `Showcase()`。

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

链接 CMake target `aimgui_md3`。独立使用时编译 `jni/src/ui/md3/*.cpp` 与本仓库固定的 ImGui 核心。内部使用 ImGui 控件 API，升级 ImGui 后需重新验证。

尺寸为逻辑 dp，按 `Theme::scale` 换算。`GetTheme()` 可修改颜色。基础控件使用 `BeginDisabled/EndDisabled`；部分组合控件另有 enabled 参数。标签支持 `文字##唯一ID`。

`BeginCard` 无论返回值如何都必须 `EndCard`。菜单、对话框和面板只在 Begin 返回 true 后调用 End。对话框／面板的 open 状态由调用方持有。日期范围先选起点，再选终点；倒序会整理为升序。

## 组件覆盖

| 接口文件 | 组件 |
| --- | --- |
| `md3.h` | 五种按钮、四种图标按钮、三种 FAB／扩展 FAB、复选框／混合态、单选框、开关、四种 Chip、连续／步进／范围滑块、Filled／Outlined 输入框 |
| `md3_layout.h` | 三种卡片、一／二／三行列表、分隔线、徽标、主／次标签栏、单／多选分段按钮、导航条／侧栏／抽屉、四种顶部应用栏、两种进度条、Snackbar、Tooltip、基本／全屏对话框、底部／侧边面板 |
| `md3_selection.h` | 下拉选择、搜索建议、菜单／子菜单、日期、日期范围、12／24 小时时间选择器 |
| `md3_extra.h` | 图片／文字轮播、底部应用栏、富提示 |

## 验证和边界

交互：`tests/run_md3_checks.sh`。原生渲染：`tests/render_md3.py`。官方网页参考与固定依赖：`tests/md3_web/`。源版本、令牌和对照范围见 [MD3_REFERENCE.md](MD3_REFERENCE.md)。

截图检查控件尺寸、形状、颜色、文字和状态。浏览器与 ImGui 的文字栅格化、阴影和动画存在差异，截图数量不代表像素级一致。Material Web 未提供的组件在参考文档中单独注明。

- 组件库和展示页已接入；旧窗口外壳及其他设置页面未整体重写。
- 移除 DEX 后不自动唤起 Android 系统软键盘；evdev 支持外接键盘。
- 未接入 TalkBack 语义树，也未从壁纸生成动态色。
- 应用字体仍沿用 Inter 与系统中文回退；对照截图固定 Roboto。
- Android 真机触摸、旋转、输入与 GLES／Vulkan 一致性需要设备验证。
