# MD3 原生实现的网页参考

Android 组件使用 Dear ImGui 原生输入与绘制。`tests/md3_web` 仅是截图基准，不进入 ELF，不依赖 WebView、Java 或 DEX。

## 固定参考版本

- 官方源码：[material-components/material-web](https://github.com/material-components/material-web/tree/37aa14b843dcaeb3a46f898679fa6fcd109b1905)，本次阅读的提交为 `37aa14b843dcaeb3a46f898679fa6fcd109b1905`。
- 实际浏览器组件：npm `@material/web` **2.5.0**，完整依赖及包完整性由 `tests/md3_web/package-lock.json` 固定。源码提交和 npm 发布包分别记录，不假定两者所有文件完全相同。
- 字体：`@fontsource/roboto` **5.2.8**，正文 400、标签 500；拉丁样例避免不同中文字体的字面差异影响对照。原生截图使用相同字体解压出的 TTF；许可证及来源校验见 `tests/md3_web/README.md`。
- Material Web 为 Apache-2.0，Roboto 为 OFL-1.1。源码仅用于阅读，npm 依赖只用于截图工具。

## 可对照组件

| 组 | 官方网页参考 | 状态 / 示例 |
| --- | --- | --- |
| Actions | Filled / tonal / outlined / elevated / text buttons | `Button`，启用与禁用 |
| Actions | Standard / filled / tonal / outlined icon buttons | `+` |
| Actions | Small / medium / large / extended FAB | `variant="primary"`，`+`，`Create` |
| Actions | Assist / filter / input / suggestion chips | 启用、选中、删除、禁用 |
| Inputs | Checkbox / radio / switch | 开、关、混合、禁用；Switch 使用 `icons` |
| Inputs | Continuous / discrete / range sliders | 0–100，40；范围 20–70 |
| Inputs | Filled / outlined text fields | `Label` / `Input text` / `Supporting text`；错误、禁用 |
| Layout | Primary / secondary tabs | `Overview` / `Activity` / `Settings` |
| Layout | List / divider | 一行、两行、三行支持文本 |
| Layout | Cards / navigation bar | 官方实验性 `labs` 组件 |
| Feedback | Linear / circular progress | 40% / 75% |
| Feedback | Filled / outlined select | `Fruit`，Apple / Banana / Cherry，`Choose a fruit` |
| Feedback | Menu / submenu | New item / Show details / More actions / Delete；含嵌套 Export |
| Feedback | Segmented buttons / badges | Day / Week / Month，单选 Day、多选 Day+Week；Badge 3；官方实验性 `labs` |
| Dialog | Basic modal dialog | Save changes? / Your changes will be saved to this project. / Cancel / Save |

该发布包没有日期、日期范围、时间、搜索栏/搜索视图、导航轨、底部/侧边面板、Snackbar、Tooltip 的对应组件。这些原生扩展不能宣称已经与官方网页组件逐像素对齐；需分别记录原生截图和交互验证。普通模态对话框另有 `md-dialog` 基准；全屏对话框没有在此包中单独实现。

## 实际生效的尺寸

阅读最新 token 文件时不能直接假定组件已采用该版本。例如 `tokens/_md-comp-slider.scss` 实际导入 `versions/v0_192/md-comp-slider`，因此 npm 2.5.0 网页滑块使用 **4 dp 轨道、20 dp 圆形手柄**。仓库同时含有 latest 目录中的 16 dp 轨道和 44 × 4 dp 手柄，但该文件并非此发布组件的实际基准。

| 项目 | 实际参考尺寸 | 官方源文件 |
| --- | --- | --- |
| 通用按钮 | 高 40，标签 14 / Medium | `tokens/versions/v0_192/_md-comp-filled-button.scss` |
| Switch | 轨道 52 × 32；手柄选中 24、未选 16、按下 28 | `tokens/versions/v0_192/_md-comp-switch.scss` |
| Slider | 轨道高 4；圆形手柄 20 | `tokens/versions/v0_192/_md-comp-slider.scss` |
| Segmented button | 高 40，图标 18 | `tokens/versions/v0_192/_md-comp-outlined-segmented-button.scss` |
| Navigation bar | 高 80，图标 24 | `tokens/versions/v0_192/_md-comp-navigation-bar.scss` |

示例文字和数值以 `jni/src/ui/md3/md3_showcase.cpp` 为准：按钮宽 112、高 40；卡片 208 × 112；标签和导航宽 680；列表宽 656；范围滑块宽 584。导航为 Home / Activity（Badge 3）/ Settings；列表标题均为 Headline，左侧 A / B / C，右侧 100+ / 24 / 56，第三行支持文字为 Supporting text / Second supporting line。

仍需区分组件能力差异：官方 `md-list-item` 没有原生图库的 selected 属性，所以第二行参考保持官方默认背景。禁用 Input Chip 两侧均保留移除按钮，但不接受操作。截图保留并报告列表的状态差异，不用修改官方内部样式隐藏它们。

截图统一主色 `#6750A4` / 暗色 `#D0BCFF`，表面色 `#FEF7FF` / 暗色 `#141218`。文本、数值、主题、缩放和字体应相同后再比较。

## 复现与验收边界

运行 `tests/md3_web/README.md` 的命令会生成真实浏览器截图、逐组件裁图和带边界坐标的 manifest。截图工具需能启动 Chromium；受限环境如果禁止浏览器所需的 socket，就将此步骤交由普通 Linux CI 执行，不用手绘图片代替官方截图。

原生与网页由不同文本栅格化器绘制，截图相似不代表行为正确。对比要分别检查：文字基线/字重、容器尺寸与圆角、选中/错误/禁用颜色、阴影和轨道；然后独立验证 Tab / Enter / Space / Escape、拖动、日期有效性和禁用输入。

本文件描述参考版本、覆盖范围与复现方法；只有实际生成的截图及检查输出才能作为截图已完成的依据，不把脚本存在视作渲染成功。
