# Build outputs

Built from this revision with Android NDK r29 (29.0.14206865), CMake Release,
C++17 and Dear ImGui 1.93.0. Live2D is disabled.

- `AImGui`: Android ARM64 native executable.
- `ime-host.dex`: optional Java 8 bytecode/D8 helper, min API 26. Place beside the ELF.
- `SHA256SUMS`: SHA-256 of both files (`sha256sum -c SHA256SUMS`).

These binaries pass build/host checks, but are not a substitute for Android
on-device compatibility checks. See the root README for deployment and limits.
