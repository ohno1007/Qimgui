# Build outputs

Build the current revision with `./build.sh`, using Android NDK r29
(29.0.14206865). The native ARM64 executable is written to
`libs/arm64-v8a/AImGui`.

GitHub Actions publishes the executable in the **Qimgui-arm64-v8a** artifact
for each successful pull request or main-branch build. Use the artifact from
the commit you are testing; older checked-in executables are removed so they
cannot be mistaken for the native MD3 build.

The Java input/clipboard host has been removed. Deploy only the ELF; see the
root README for external-keyboard input and Android device validation limits.
