# Qimgui MD3 内核组件

这是 Qimgui 的纯 C、整数运算 MD3 移植，面向 GKI 外部模块构建。它包含：

- 35 个 MaterialShapes 轮廓、Roboto 400 ASCII 字形和 Qimgui 图标；
- ARGB8888 预乘 alpha 软件栅格器；
- 按钮、图标按钮、FAB、复选框、单选框、开关、Chip、滑块、范围滑块、文本框；
- 导航栏、Rail、Drawer、Tabs、按钮组、Toolbar、Card、列表、进度、对话框和底部 Sheet；
- `/dev/qimgui_md3` 测试模块接口，输出 CPU 帧缓冲并接收简单指针/按键事件。

运行时不使用 C++、浮点、GL/EGL、堆分配、线程或 Dear ImGui。模块只负责生成 CPU 像素帧，显示控制器接入需要设备内核提供的正式 DRM/KMS 客户端接口；本模块不替换既有驱动回调，也不解析私有符号。

## 构建

```sh
dddk build --target android15-6.6 --module "$PWD/kernel/md3" -- -j2
```

设备的 ACK 代际、KMI、配置和模块签名必须与实际内核匹配。模块参数 `width`、`height` 限制在 4096 以内，默认 640×480。`QMD3_IOC_INFO` 查询帧尺寸，`QMD3_IOC_INPUT` 更新输入，`QMD3_IOC_RENDER` 生成一帧，读取设备文件取得顶行优先的预乘 ARGB8888 数据。

形状和字体数据由 `scripts/generate_kernel_md3_assets.py` 生成，模块构建不需要 Python。生成数据的许可和来源记录在 `generated/README.md` 与 `generated/provenance.json`。
