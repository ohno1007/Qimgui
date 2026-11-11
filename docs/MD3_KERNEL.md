# GKI 内核版 MD3

`kernel/md3/` 是 Qimgui 的纯 C MD3 组件移植，服务于不使用液态玻璃和辉光的组件页面。它保留 MD3 的颜色角色、Rail/Bar/Drawer 导航、Tabs、按钮组、Card、列表、进度、对话框、Sheet、文本框、滑块、开关和 MaterialShapes 轮廓。

运行层由三部分组成：`qmd3.h` 定义整数像素帧、主题、输入和生命周期；`src/canvas.c` 负责预乘 ARGB8888 软件绘制；`src/widgets.c` 与 `src/layout.c` 组合组件。字体、图标和 35 个形状由 `generated/` 固化，模块运行时不解析文件，也不使用 C++、浮点或图形 API。

```text
kernel/md3/
├── include/       公共 C API
├── src/           核心、栅格、控件和布局
├── generated/     Roboto、图标、MaterialShapes 和来源记录
├── module.c       GKI 外部模块 /dev/qimgui_md3
└── Makefile       Kbuild 入口
```

模块默认生成 640×480 CPU 帧，使用 `width=` 和 `height=` 参数调整到不超过 4096。`QMD3_IOC_INPUT` 写入指针和按键，`QMD3_IOC_RENDER` 生成一帧，读取设备文件获取顶行优先的预乘 ARGB8888。像素帧由调用方负责接入设备认可的显示客户端接口；模块不接管现有 DRM 驱动的回调和资源。

## 验证

主机回归：

```sh
source /workspace/.qimgui/env.sh
scripts/run_kernel_md3_checks.sh
```

GKI 6.6 编译：

```sh
dddk build --target android15-6.6 --module "$PWD/kernel/md3" -- -j2
```

本次验证生成的 ARM64 模块位于 `build/artifacts/qimgui_md3-gki66-arm64.ko`。该文件针对 DDK 的 `android15-6.6-lts` 构建目录；实际手机仍需匹配 ACK 代际、KMI、内核配置和模块签名。
