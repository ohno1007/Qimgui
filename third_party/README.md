# third_party — Live2D Cubism SDK drop-in

The Live2D integration is **off by default** and requires two components from
the official **Cubism SDK for Native** package, which you must download
yourself after agreeing to Live2D's license (they cannot be redistributed in
this repo, and are `.gitignore`d here):

```
third_party/
├── Core/                       # from the SDK package: Core/
│   ├── include/Live2DCubismCore.h
│   └── lib/android/arm64-v8a/libLive2DCubismCore.a
└── CubismNativeFramework/      # from the SDK package: Framework/
    └── CMakeLists.txt , src/ , ...
```

> **Version match matters.** Copy `Core/` **and** `Framework/` from the *same*
> SDK package — mismatched Core/Framework versions will not link or run.

Get the SDK here: https://www.live2d.com/en/sdk/download/native/
(Cubism 5 SDK for Native). Unzip and copy:

- `<sdk>/Core`      → `third_party/Core`
- `<sdk>/Framework` → `third_party/CubismNativeFramework`

Then build with `-DAIMGUI_LIVE2D=ON`. See [docs/LIVE2D.md](../docs/LIVE2D.md).
