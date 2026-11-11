# kernel/

`qimgui.ko`：在内核态绘制并显示 MD3 界面的模块。设计、约束和验证范围见
[内核态移植](../docs/KERNEL_PORT.md)。

| 目录 | 内容 |
| --- | --- |
| `src/` | 模块入口、DRM 显示接入、input handler、帧循环、窗口与触摸判定、符号解析 |
| `compat/` | 内核侧 C++ 运行时、arena 分配器、libm 子集、ImGui 配置 |
| `compat/include/` | freestanding 的 C 与 C++ 头文件，替代 libc 与 libstdc++ |
| `raster/` | 定点软件光栅器，把 ImGui draw data 画成预乘 ARGB8888 |
| `ui/` | 内核侧的 ImGui 上下文、字体、MD3 页面，以及 draw data 转定点 |

组件实现仍在 `jni/src/ui/md3/`，ImGui 仍在 `third_party/imgui/`：这里不复制它们，构建时按原布局暂存编译。

## 构建

```bash
make -C kernel KDIR=/path/to/kernel/source \
     ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- QK_CXX=aarch64-linux-gnu-g++
```

必须使用目标内核自己的工具链；内核用 clang 构建时传 `CC=clang LLVM=1 QK_CXX=clang++`。
产物是 `kernel/.build/qimgui.ko`。

## 运行

```bash
adb push kernel/.build/qimgui.ko /data/local/tmp/
adb shell su -c 'insmod /data/local/tmp/qimgui.ko width=640 height=1280 pos_x=220 pos_y=560'
adb shell su -c 'cat /proc/qimgui'
adb shell su -c 'dmesg | grep qimgui'
```

`enabled=0` 撤下画面，`grab=0` 把触摸完全交还 Android。
**接入成功后模块会钉住自己直到重启**，不要强制 `rmmod`。

## 主机验证

```bash
tests/run_kernel_checks.sh
```

数学子集、分配器、光栅器、窗口与触摸判定、完整 ImGui + MD3 帧，以及 freestanding 编译和未定义符号检查。
渲染结果写在 `build/kernel-checks/*.ppm`。
