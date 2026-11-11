# kernel/

`qimgui.ko`：在内核态绘制并显示 MD3 界面的模块。设计、约束和验证范围见
[内核态移植](../docs/KERNEL_PORT.md)。

| 目录 | 内容 |
| --- | --- |
| `src/` | 模块入口、DRM 显示接入、input handler、帧循环、窗口与触摸判定、符号解析 |
| `compat/` | 内核侧 C++ 运行时、arena 分配器、libm 子集、ImGui 配置 |
| `compat/include/` | freestanding 的 C 与 C++ 头文件，替代 libc 与 libstdc++ |
| `raster/` | 定点软件光栅器，把 ImGui draw data 画成预乘 ARGB8888 |
| `ui/` | 内核侧的 ImGui 上下文与字体、应用层（`UiState`、设置、触感空实现），以及 draw data 转定点 |

界面本身不在这里：`AImGui` 主界面仍是 `jni/src/ui/ui.cpp` 和 `jni/src/ui/main_ui.cpp`，
组件在 `jni/src/ui/md3/`，ImGui 在 `third_party/imgui/`。构建时按原布局暂存编译，不复制。

## 构建

```bash
kernel/build.sh /path/to/kernel-source
LLVM=1 kernel/build.sh /path/to/kernel-source    # clang 构建的内核
```

`KDIR` 必须是**目标设备那个内核**的源码树（已 `make modules_prepare`），工具链也要和它一致：
vermagic、CFI、结构体布局和模块签名都要对上，否则 `insmod` 会被拒。
产物是 `kernel/.build/qimgui.ko`。

## 运行

```bash
adb push kernel/.build/qimgui.ko /data/local/tmp/
adb shell su -c 'insmod /data/local/tmp/qimgui.ko'          # 整屏 plane，主界面
adb shell su -c 'cat /proc/qimgui'
adb shell su -c 'dmesg | grep qimgui'
```

`gallery=1` 只显示组件画廊；`font_path=` 指定中文字体；`settings_path=` 改设置文件位置。
`enabled=0` 撤下画面，`grab=0` 把触摸完全交还 Android。
**接入成功后模块会钉住自己直到重启**，不要强制 `rmmod`。

## 加载失败时

`insmod` 只给一个 errno，而 `ENOEXEC` 可以是十几种不同的拒绝。`kernel/qkload.c` 是个诊断加载器：
它检查文件本身（ELF 头、大小、vermagic、有无符号 CRC），调用 `finit_module`，
然后**自己通过系统调用读内核环形缓冲区**（设备上 dmesg 常被限制），把相关的那几行打出来。

```bash
aarch64-linux-gnu-gcc -static -O2 -o qkload kernel/qkload.c
adb push qkload /data/local/tmp/ && adb shell su -c 'chmod 755 /data/local/tmp/qkload'
adb shell su -c '/data/local/tmp/qkload /data/local/tmp/qimgui.ko'
```

`-n` 只检查不加载；`-f` 忽略 vermagic（符号 CRC 仍然校验，所以仍是安全的那一半）。
它**不会**设置 `MODULE_INIT_IGNORE_MODVERSIONS`：结构体定义对不上还硬装，是把机器搞坏的标准做法。

## 主机验证

```bash
tests/run_kernel_checks.sh
```

数学子集、分配器、光栅器、窗口与触摸判定、完整 ImGui + MD3 帧，以及 freestanding 编译和未定义符号检查。
渲染结果写在 `build/kernel-checks/*.ppm`。
