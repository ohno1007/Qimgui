# MD3Clock / Tempo

一个通过已安装 Qimgui MD3 SDK 构建的 C++17 示例。左侧为 Alarm、Clock、Timer、Stopwatch 图标导航，右侧显示对应页面。默认打开计时器；窗口可拖动、调整大小，并支持浅色／深色主题。

源码分为入口 `src/main.cpp`、窗口与导航 `src/app.*`、页面 `src/pages.*`、业务状态 `src/model.*`。组件和 Android 运行时通过 `Qimgui::MD3`、`Qimgui::AndroidOverlay` 链接，示例不包含组件／平台实现或图库代码。

## 使用随包 SDK 构建

从包含 `sdk/arm64-v8a/` 和 `examples/md3_clock/` 的解压目录执行：

```bash
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
cmake -S examples/md3_clock -B build/md3-clock \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DQimguiMD3_DIR="$PWD/sdk/arm64-v8a/lib/cmake/QimguiMD3"
cmake --build build/md3-clock --parallel
```

如果使用完整源码，请先按 [完整构建与接入说明](../../docs/MD3_CLOCK.md) 构建、安装带 `AndroidOverlay` 的 SDK，再设置相应 `QimguiMD3_DIR`。本示例的 CMake 始终消费安装包。

## 手机测试

将输出的 `MD3Clock` 复制到 Android ARM64 手机的可执行目录，在 root 终端执行：

```sh
chmod 755 MD3Clock
./MD3Clock
```

顶部空白区域可拖动；右下角可调整大小；太阳／月亮切换主题；减号隐藏；音量键恢复；叉号退出。

录屏请选择“整个屏幕”，启动后等待约 3 秒。新版 SDK 宿主会自动维护录屏显示镜像，并输出 `[recording]` 检测／创建日志；ROM 兼容性仍需手机确认。

Timer 和 Stopwatch 在切换页面、隐藏窗口后继续计时。Alarm 只编辑本次运行的闹钟数据，不注册系统闹钟或后台通知；全部状态随进程结束而丢失。城市时钟使用固定 UTC 偏移。

`RunOverlay` 从 `main()` 调用一次并管理整个进程宿主。回调返回 `false` 后宿主释放输入并结束进程；由于现有触摸线程持有 ImGuiIO，退出使用 `std::_Exit`。需要持久化时，应在回调返回前保存，不能依赖 `main()` 对象的析构。完整契约见 [运行时生命周期](../../docs/MD3_CLOCK.md#runoverlay-的生命周期)。

当前云环境没有 Android 真机，交叉编译和软件预览之后仍需手机测试。
