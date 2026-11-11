# MD3Clock：使用 SDK 的左右布局示例

`examples/md3_clock/` 是一个独立的 C++17 应用工程，通过已安装的 SDK 构建。它的 CMake 只使用 `find_package(QimguiMD3)` 和导出的 targets，没有将组件实现、图库或 Android 平台源码复制进示例。

窗口名为 **Tempo**。左侧是图标在上、文字在下的 MD3 Navigation Rail，选中图标带胶囊底色；右侧直接显示当前页面。导航始终保留在左侧，窗口较小时只滚动右侧内容。

## 页面与操作

| 页面 | 内容 | 使用的 SDK 组件 |
| --- | --- | --- |
| Alarm | 闹钟列表、启用开关、新增和编辑时间／标签 | Switch、Button、Dialog、Select、TextField、Divider |
| Clock | 本地日期时间、12／24 小时切换、北京／东京的固定 UTC 偏移时间 | Switch、ListItem、Divider |
| Timer | 预设时长、自定义分钟数、开始／暂停／重置、完成状态 | ButtonGroup、Slider、Button、WavyLinearProgress |
| Stopwatch | 开始／暂停、计圈、分段与累计时间、重置 | Button、Divider，配合 ImGui 表格 |

默认打开 Timer。计时器和秒表使用单调时钟计算经过时间，切换页面或隐藏窗口时仍会更新；进程结束后状态丢失。Alarm 是本次运行内的编辑示例，不注册 Android 系统闹钟、不发送后台通知。Clock 的本地时间来自系统，城市列表使用明确的固定 UTC 偏移，没有完整时区数据库或夏令时规则。

- 拖动顶部空白区域移动窗口，拖动右下角调整大小。
- 顶部太阳／月亮按钮切换浅色和深色主题。
- 减号隐藏窗口，音量键可以隐藏或恢复窗口；叉号结束程序。
- 文字字段可使用外接键盘输入。组件支持 SDK 自带的键盘导航。

## 工程拆分

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 接入运行时，采样单调时钟／系统时间，将输入区域交回运行时 |
| `src/app.hpp`、`src/app.cpp` | 窗口位置、大小、主题、左右布局和导航 |
| `src/pages.hpp`、`src/pages.cpp` | 四个页面，只组合 SDK 公共组件与 ImGui 布局 |
| `src/model.hpp`、`src/model.cpp` | 页面状态、计时器、秒表、计圈和时间格式化；只依赖 C++ 标准库 |

组件头文件为 `<qimgui/md3.hpp>`。Android 宿主头文件为 `<qimgui/android_overlay.hpp>`，对应独立的可选 `Qimgui::AndroidOverlay` target。可移植的 `Qimgui::MD3` 核心没有新增 Android 或渲染后端依赖。

应用的链接关系为：

```cmake
find_package(QimguiMD3 0.1 REQUIRED CONFIG COMPONENTS MD3 AndroidOverlay)
add_executable(MD3Clock src/main.cpp src/app.cpp src/model.cpp src/pages.cpp)
target_link_libraries(MD3Clock PRIVATE Qimgui::MD3 Qimgui::AndroidOverlay)
```

## 用测试包中的 SDK 编译

测试包提供同级的 `sdk/arm64-v8a/` 和 `examples/md3_clock/`。在安装了 CMake、Android NDK r29 的开发电脑上，进入解压目录后执行：

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
cmake -S examples/md3_clock -B build/md3-clock \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DQimguiMD3_DIR="$PWD/sdk/arm64-v8a/lib/cmake/QimguiMD3"
cmake --build build/md3-clock --parallel
```

结果为 `build/md3-clock/MD3Clock`。SDK 和示例必须使用一致的平台、ABI、ImGui 配置及 C++ 运行库。NDK 的默认包搜索范围可能屏蔽仅通过 `CMAKE_PREFIX_PATH` 指定的外部安装目录，因此上述命令直接设置 `QimguiMD3_DIR`。

## 从完整源码先构建 SDK

在 Qimgui 仓库根目录执行。构建运行时需要 Python 3，用于将仓库中的 Roboto 字体生成嵌入数据。

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
cmake -S libraries/md3 -B build/md3-clock-sdk-build \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DQIMGUI_MD3_BUILD_ANDROID_OVERLAY=ON \
  -DQIMGUI_MD3_BUILD_SHOWCASE=OFF -DQIMGUI_MD3_INSTALL=ON \
  -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build/md3-clock-sdk-build --parallel
cmake --install build/md3-clock-sdk-build --prefix "$PWD/build/md3-clock-sdk"

cmake -S examples/md3_clock -B build/md3-clock \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DQimguiMD3_DIR="$PWD/build/md3-clock-sdk/lib/cmake/QimguiMD3"
cmake --build build/md3-clock --parallel
```

`QIMGUI_MD3_BUILD_ANDROID_OVERLAY` 默认关闭。开启它需要 Android NDK toolchain；普通桌面组件项目保持关闭即可。安装后可以移动 SDK 目录，示例不需要回到 Qimgui 源码树中查找实现。

## 在 Android 上运行

将 `MD3Clock` 放到手机上的可执行目录，例如 `/data/local/tmp/md3-clock/`，在 root 终端进入该目录：

```sh
chmod 755 MD3Clock
./MD3Clock
```

这是 Android ARM64 ELF 程序，通过现有 Android overlay 宿主显示窗口。当前云环境没有连接 Android 真机；主机交互验证、软件渲染预览或交叉编译不能代替手机运行验证。

录屏请选择“整个屏幕”，启动后等待约 3 秒。宿主会自动维护录屏显示镜像，先输出 `[overlay] recording mirror enabled`，检测成功后输出 `[recording] parsed displays=...` 和 `[recording] mirrored window to stack ...`。停止录屏或退出程序会清理镜像。不同 ROM 的接口支持仍需手机确认；若画面仍缺失，请保留本次运行的 `[recording]` 日志。

## RunOverlay 的生命周期

`RunOverlay(options, callback, userData)` 是从 `main()` 调用一次的进程宿主。它创建 ImGui context、Surface、GLES3 渲染器和输入处理，并加载字体。每帧在 `ImGui::NewFrame()` 之后调用应用回调；回调结束后由宿主提交绘制。

回调收到的 `Frame` 包含屏幕大小、字体、可见状态和触摸拦截区域。示例先调用 `Update()` 更新业务状态，再绘制页面，最后将窗口边界写入 `inputBounds`。隐藏时回调仍执行，输入区域应为零。弹窗开启时，宿主会拦截整个屏幕以处理弹窗外点击。

应用不能在回调中创建／销毁 context、提交帧或抛出异常；ImGui 的 Begin／End、样式和字体栈必须配对。回调返回 `false` 请求退出。初始化失败会返回非零状态；输入线程启动后的正常退出会先释放输入、关闭键盘和渲染器，再结束进程。现有触摸线程保留了 ImGuiIO 引用，因此宿主使用 `std::_Exit`，不会依赖 `main()` 中对象的析构。需要保存的业务状态必须在回调返回 `false` 之前写出。

## 验证

完整源码中可运行：

```bash
tests/run_md3_clock_checks.sh
tests/run_overlay_runtime_checks.sh
tests/run_recording_mirror_checks.sh
```

该示例的状态与页面分离，便于用人工时间样本检查暂停／恢复、跨页面计时、完成边界及计圈行为，再通过原生 ImGui 输入检查导航和页面操作。宿主回归测试执行真实帧循环，以模拟 Android 服务验证录屏发现调用；镜像测试另行检查接口、重连和资源清理。预览采用实际绘制数据的软件渲染，不是 Android 手机截图。

更多组件接口见 [组件框架](MD3_FRAMEWORK.md) 和 [Expressive 组件](MD3_EXPRESSIVE.md)。
