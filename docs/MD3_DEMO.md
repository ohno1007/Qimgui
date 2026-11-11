# MD3 独立组件测试

`MD3Demo` 是专门验证组件的独立 ARM64 可执行文件。主程序 `AImGui` 也已统一为 MD3，并提供一个液体玻璃效果开关，详见 [MD3_MAIN.md](MD3_MAIN.md)。二者共用组件库和平台基础代码；`MD3Demo` 与 [Tempo 示例](MD3_CLOCK.md) 共同使用 `Qimgui::AndroidOverlay` 宿主，统一处理 Surface、字体、输入、GLES 3 和录屏镜像。组件页面不初始化玻璃背景采集、辉光或传感器，不读写 `aimgui.conf`。

## 构建与运行

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r29
./build.sh
# libs/arm64-v8a/AImGui
# libs/arm64-v8a/MD3Demo
```

先退出 `AImGui`，将 `MD3Demo` 放到设备可执行目录，在 root 终端运行：

```sh
chmod 755 MD3Demo
./MD3Demo
```

拖动标题区域移动窗口，右下角调整大小；音量键隐藏／恢复。窗口提供明暗主题、显示比例、组件分类与交互样例。显示比例同时改变控件、字体与间距，区别于拖动窗口改变可用空间。较宽的组件展示可横向滚动；样例状态只在本次进程中保留。

## 手动测试样例

| 场景 | 操作 | 预期结果 |
| --- | --- | --- |
| 分类展示 | 依次打开按钮与标签、输入与选择、导航、容器与反馈、日期与时间、媒体与菜单、形状、表现力组件 | 每类组件均可操作，切换页面后所选状态保留 |
| Expressive | 切换工具栏选中态，分别点击分割按钮的主按钮与下拉区域，切换按钮组，拖动进度与波形滑块 | 主动作和菜单各自响应；按钮组支持单／多选并有按压形变；波幅、波长和线宽即时更新 |
| 减少动态效果 | 打开表现力组件底部的 Reduce motion | 状态过渡立即完成，波浪与不定进度保持静态且可见 |
| MaterialShapes | 在“形状”中切换预设，调整旋转与大小，切换填充／描边，再滚动查看全部 35 种形状 | 轮廓随预设变化；凹形填充完整；缩放、旋转和主题切换后显示正常 |
| 按钮与禁用态 | 在交互样例中点击计数按钮，再关闭启用开关并重试 | 计数递增；禁用后计数不再变化，按钮显示禁用态 |
| 设置联动 | 切换复选框与通知开关，拖动滑块 | 结果区域同步显示当前状态和数值 |
| 空值校验 | 重置样例后直接保存空名称 | 显示错误提示，不提交空名称 |
| 有效提交 | 点击填入示例，再保存并确认 | 显示已保存名称及 Snackbar；取消确认不会提交 |
| 样例重置 | 修改多个值后重置 | 计数、设置、表单和反馈状态恢复初始值，可重复测试 |
| 主题与比例 | 切换明暗主题、调整显示比例 | 各组件样式随主题变化，窗口操作和分类入口保持可达 |
| 窗口与旋转 | 移动、缩放窗口，旋转手机，按音量键隐藏／恢复 | 窗口留在屏幕范围内；隐藏时释放触摸拦截，恢复后可继续操作 |
| 退出 | 点击窗口关闭按钮 | 程序结束并释放触摸拦截 |

文本框支持外接键盘；不会自动唤起系统软键盘。“填入示例”可在只有触屏时完成表单流程。默认拦截窗口范围内的触摸，依赖设备允许创建 `/dev/uinput`；外部区域继续转发。

录屏请选择“整个屏幕”并等待约 3 秒。共享宿主输出 `[overlay] recording mirror enabled` 及 `[recording]` 检测／镜像日志。Android 17 的新版录屏适配仍需设备视频确认，不能仅以截图可见或进程正常退出作为录制成功的依据。

## 自动回归与截图

```bash
tests/run_md3_checks.sh
tests/run_md3_shapes_checks.sh
tests/run_md3_expressive_checks.sh
tests/run_md3_package_checks.sh
tests/run_md3_demo_checks.sh
tests/run_overlay_runtime_checks.sh
build/md3-demo-checks/md3-demo-test --capture build/md3-demo-captures \
  tests/md3_web/fonts/Roboto-400.ttf tests/md3_web/fonts/Roboto-500.ttf
python3 tests/render_md3.py build/md3-demo-captures --output build/md3-demo-preview
```

截图使用实际窗口产生的 ImGui 绘制数据，经 Mesa 渲染；不是 Android 真机截图。中文截图使用主机 Noto Sans CJK，Android 使用设备字体回退。自动回归覆盖交互与布局；设备输入、Surface 和 EGL 生命周期仍需真机验证。

窗口与交互样例实现位于 `jni/src/demo/md3_demo.cpp`，Android 入口 `jni/src/demo/main.cpp` 只连接状态和 `RunOverlay` 回调；GLES 渲染器位于共享的 `jni/src/runtime/`。在仓库根工程内，将 `aimgui_md3_demo` 链接到已有 ImGui 程序后，持有 `aimgui::md3demo::State` 并逐帧调用 `Draw(state, &running, displaySize)` 即可复用测试窗口。外部 SDK 用户通过 `Qimgui::MD3` 使用组件，通过可选 `Qimgui::MD3Showcase` 使用图库；完整接入方式见 [框架文档](MD3_FRAMEWORK.md)。

MaterialShapes 的接口示例、官方数据来源与形状目录截图复现见 [MD3_SHAPES.md](MD3_SHAPES.md)。
