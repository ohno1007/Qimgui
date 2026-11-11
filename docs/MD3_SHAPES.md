# MaterialShapes 原生形状

`ui/md3_shapes.h` 提供 AndroidX Material 3 Expressive `MaterialShapes` 的全部 **35 种预设**。数据来自官方 `RoundedPolygon.normalized()` 输出的三阶贝塞尔曲线，在 C++ 中通过 ImGui 绘制，保留官方圆角、平滑度和凹凸轮廓。Android 程序不需要 Kotlin、Compose、Java、DEX 或额外图片资源。

主窗口在“组件 → 完整组件 → 形状”中展示；`MD3Demo` 在“组件分类 → 形状”中展示。页面支持选择预设、调整旋转和大小、切换填充／描边，并列出所有形状。形状使用当前 MD3 主题颜色，窗口保持既有的左侧导航、右侧内容布局。

## 使用

链接 `aimgui_md3`，在已有 ImGui 帧和窗口中调用：

```cpp
#include "ui/md3.h"
#include "ui/md3_shapes.h"

namespace md = aimgui::md3;

// 初始化主题，通常放在窗口的 PushTheme / PopTheme 范围内。
md::SetTheme(false, 1.0f);

// 布局尺寸为 dp，使用主题 primary 色；此项只展示形状，不处理点击。
md::Shape("flower", md::MaterialShape::Flower, ImVec2(96, 96));
ImGui::SameLine();
md::Shape("heart-outline", md::MaterialShape::Heart,
          ImVec2(96, 96), 15.f, true); // 顺时针 15 度、2dp 描边

// 自定义绘制使用物理像素，适合嵌入已有控件。
const ImVec2 origin = ImGui::GetCursorScreenPos();
const float scale = md::GetTheme().scale;
const ImVec2 size(144.f * scale, 96.f * scale);
ImGui::Dummy(size); // DrawShape 本身不占据 ImGui 布局空间
md::DrawShape(ImGui::GetWindowDrawList(), md::MaterialShape::Cookie6Sided,
              origin, size, ImGui::GetColorU32(md::GetTheme().primaryContainer),
              30.f, ImGui::GetColorU32(md::GetTheme().primary), 2.f * scale);
```

`ShapeName(shape)` 返回官方预设名。`GetShapeGeometry(shape)` 返回只读的 `ShapeGeometry`，包含 `name`、`cubics`、`cubic_count` 和官方中心 `center`。每条 `ShapeCubic` 含起点、两个控制点和终点。这些数据可在没有 ImGui context 时读取，生命周期为整个进程；调用方不能修改。

`DrawShape` 支持同时填充和描边，将相应颜色的 alpha 设为零即可关闭该部分。描边宽度为物理像素；`Shape` 的 `outlined=true` 使用 2dp 描边。曲线按屏幕尺寸自适应细分，凹形状使用凹多边形三角剖分填充。两种入口都不会把图形自动变成按钮；点击、裁剪或子控件布局由调用方负责。

## 尺寸与旋转

- `ShapeFit::Contain` 为默认值：以目标宽高的较小值等比缩放官方归一化坐标，旋转后若超出目标区域再缩小；保留原始比例，不把非正方形轮廓强行放大填满。
- `ShapeFit::Stretch` 分别按目标宽高缩放，再将实际曲线边界居中。Compose `toShape` 按路径控制点边界居中，因此部分形状的位置有所不同，曲线几何保持一致。旋转后可能超出目标区域，必要时用绘制列表的裁剪矩形限制显示。
- 角度单位为度，正值顺时针，围绕官方中心旋转。这是普通相对旋转，不等同于上游 `toPath(startAngle)` 对起始锚点的重定位。
- 可见描边会从目标区域四边各预留半个描边宽度；抗锯齿边缘可能额外延伸约 1px。
- `Shape` 的尺寸随 `Theme::scale` 换算，`DrawShape` 的坐标、尺寸和线宽不再缩放。无效枚举返回空几何；非法绘制参数不会产生图形。

## 预设目录

名称与上游 API 一致，可通过 `MaterialShape` 枚举访问。

| | | | | |
| --- | --- | --- | --- | --- |
| Circle | Square | Slanted | Arch | Fan |
| Arrow | SemiCircle | Oval | Pill | Triangle |
| Diamond | ClamShell | Pentagon | Gem | Sunny |
| VerySunny | Cookie4Sided | Cookie6Sided | Cookie7Sided | Cookie9Sided |
| Cookie12Sided | Ghostish | Clover4Leaf | Clover8Leaf | Burst |
| SoftBurst | Boom | SoftBoom | Flower | Puffy |
| PuffyDiamond | PixelCircle | PixelTriangle | Bun | Heart |

本次范围为这些预设的曲线数据和原生显示接口。未移植 `Morph` 形状插值、任意 `RoundedPolygon` 构造器、Compose 的布局／语义系统或按轮廓裁剪图片。

## 来源与再生

- 官方 API：[MaterialShapes](https://developer.android.com/reference/kotlin/androidx/compose/material3/MaterialShapes)。上游标记为 `ExperimentalMaterial3ExpressiveApi`，本库固定源码版本，不随在线文档自动变化。
- 官方源码：[AndroidX `MaterialShapes.kt`](https://github.com/androidx/androidx/blob/587b8da057e3dfa7fb908e4b62b1bb84271eb20d/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/MaterialShapes.kt)，固定提交 `587b8da057e3dfa7fb908e4b62b1bb84271eb20d`。
- 原始文件 SHA-256：`08c29828003344914ee0d6783f213b37caae650175cd004ba321eaf3a7d6e434`。

曲线由固定的官方数值算法导出，未把参考图片描摹成近似多边形。导出器保留 `MaterialShapes` 类体，使用 `androidx.graphics:graphics-shapes-desktop:1.1.0` 执行圆角、平滑和归一化；所需的 Compose `Offset`／二维 `Matrix` 值类型通过固定源码公式适配。共导出 943 条三阶曲线。

来源与许可位于 [`third_party/material_shapes/`](../third_party/material_shapes/)，包含官方原始文件、`provenance.json`、Apache-2.0 [`LICENSE`](../third_party/material_shapes/LICENSE) 和 [`NOTICE`](../third_party/material_shapes/NOTICE)。`material_shapes.json` 保存导出数据及边界，`material_shapes_data.inc` 供 C++ 使用。

再生需要 Python 3、Java 17+。第一次执行会下载固定版本的 Kotlin 编译器与 JVM 依赖，并校验 [`dependencies.lock.json`](../scripts/material_shapes/dependencies.lock.json) 中的 SHA-256；缓存默认位于 `build/material-shapes-export/`。

```bash
python3 scripts/material_shapes/export.py         # 更新生成数据
python3 scripts/material_shapes/export.py --check # 再生并比较，不修改已生成文件
```

普通 CMake／NDK 构建直接使用仓库内的数据，不下载 JVM 依赖。

## 验证与截图

```bash
tests/run_md3_shapes_checks.sh
build/md3-shapes-checks/md3-shapes-test --capture build/md3-shapes-captures \
  tests/md3_web/fonts/Roboto-400.ttf tests/md3_web/fonts/Roboto-500.ttf
python3 tests/render_md3.py build/md3-shapes-captures --output build/md3-shapes-preview
```

输出 `md3-shapes-light.png` 和 `md3-shapes-dark.png`，均包含 35 种预设。截图由实际原生 ImGui 绘制数据通过 Mesa 渲染，属于主机预览。Android 设备的触摸、Surface 生命周期和最终 GPU 显示仍需真机验证。
