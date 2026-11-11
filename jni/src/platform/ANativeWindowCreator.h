/*
 * MIT License
 *
 * Copyright (c) 2023 AFan4724
 * Project: https://github.com/AFan4724/AndroidSurfaceImgui-Enhanced
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef A_NATIVE_WINDOW_CREATOR_H // !A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include <android/native_window.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <elf.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <climits>
#include <algorithm>
#include "platform/display_dump.h"

// Log system configuration
#ifndef SURFACE_LOG_TAG
#define SURFACE_LOG_TAG "AImGui"
#endif

#ifndef SURFACE_LOG_ENABLE
#define SURFACE_LOG_ENABLE 1  // Set to 0 to completely disable logging
#endif

// Log level control
#ifndef SURFACE_LOG_LEVEL
#define SURFACE_LOG_LEVEL_ERROR   1
#define SURFACE_LOG_LEVEL_WARN    2
#define SURFACE_LOG_LEVEL_INFO    3
#define SURFACE_LOG_LEVEL_DEBUG   4
#define SURFACE_LOG_LEVEL         SURFACE_LOG_LEVEL_DEBUG  // Default DEBUG level
#endif

// Unified log macro definitions
#if SURFACE_LOG_ENABLE
    #define SURFACE_LOG_ERROR(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_ERROR) \
                __android_log_print(ANDROID_LOG_ERROR, SURFACE_LOG_TAG, "[-] " fmt __VA_OPT__(, ) __VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_WARN(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_WARN) \
                __android_log_print(ANDROID_LOG_WARN, SURFACE_LOG_TAG, "[!] " fmt __VA_OPT__(, ) __VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_INFO(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_INFO) \
                __android_log_print(ANDROID_LOG_INFO, SURFACE_LOG_TAG, "[+] " fmt __VA_OPT__(, ) __VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_DEBUG(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                __android_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[*] " fmt __VA_OPT__(, ) __VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_TRACE(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                __android_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[=] " fmt __VA_OPT__(, ) __VA_ARGS__); \
        } while(0)
#else
    #define SURFACE_LOG_ERROR(fmt, ...)   ((void)0)
    #define SURFACE_LOG_WARN(fmt, ...)    ((void)0)
    #define SURFACE_LOG_INFO(fmt, ...)    ((void)0)
    #define SURFACE_LOG_DEBUG(fmt, ...)   ((void)0)
    #define SURFACE_LOG_TRACE(fmt, ...)   ((void)0)
#endif

#if SURFACE_LOG_ENABLE
#define SURFACE_STARTUP_TRACE(stage) std::fprintf(stderr, "[surface] %s\n", stage)
#else
#define SURFACE_STARTUP_TRACE(stage) ((void)0)
#endif

#define ResolveMethod(ClassName, MethodName, Handle, MethodSignature)                                                                    \
    ClassName##__##MethodName = reinterpret_cast<decltype(ClassName##__##MethodName)>(symbolMethod.Find(Handle, MethodSignature));       \
    if (nullptr == ClassName##__##MethodName)                                                                                            \
    {                                                                                                                                    \
        SURFACE_LOG_ERROR("Method not found: %s -> %s::%s", MethodSignature, #ClassName, #MethodName); \
    }

namespace android {
    namespace detail {
        namespace ui {
            // A LayerStack identifies a Z-ordered group of layers. A layer can only be associated to a single
            // LayerStack, but a LayerStack can be associated to multiple displays, mirroring the same content.
            struct LayerStack
            {
                uint32_t id = UINT32_MAX;
            };

            enum class Rotation
            {
                Rotation0 = 0,
                Rotation90 = 1,
                Rotation180 = 2,
                Rotation270 = 3
            };

            // A simple value type representing a two-dimensional size.
            struct Size
            {
                int32_t width = -1;
                int32_t height = -1;
            };

            // Transactional state of physical or virtual display. Note that libgui defines
            // android::DisplayState as a superset of android::ui::DisplayState.
            struct DisplayState
            {
                LayerStack layerStack;
                Rotation orientation = Rotation::Rotation0;
                Size layerStackSpaceRect;
            };

            typedef int64_t nsecs_t; // nano-seconds
            struct DisplayInfo
            {
                uint32_t w{0};
                uint32_t h{0};
                float xdpi{0};
                float ydpi{0};
                float fps{0};
                float density{0};
                uint8_t orientation{0};
                bool secure{false};
                nsecs_t appVsyncOffset{0};
                nsecs_t presentationDeadline{0};
                uint32_t viewportW{0};
                uint32_t viewportH{0};
            };

            enum class DisplayType
            {
                DisplayIdMain = 0,
                DisplayIdHdmi = 1
            };

            struct PhysicalDisplayId
            {
                uint64_t value;
            };

            struct Rect
            {
                int32_t left;
                int32_t top;
                int32_t right;
                int32_t bottom;
            };
        }

        struct String8;

        struct LayerMetadata;

        struct Surface;

        struct SurfaceControl;

        struct SurfaceComposerClientTransaction;

        struct SurfaceComposerClient;

        template <typename any_t>
        struct StrongPointer
        {
            union
            {
                any_t *pointer;
                char padding[sizeof(std::max_align_t)];
            };

            inline any_t *operator->() const { return pointer; }
            inline any_t *get() const { return pointer; }
            inline explicit operator bool() const { return nullptr != pointer; }
        };

        // Walk the dynamic symbol table of a shared object on disk and invoke
        // visit(name) for every *defined* exported symbol. visit returns true
        // to stop early. Reads straight off disk so it works regardless of how
        // the .so was loaded.
        template <typename Fn>
        inline void EnumerateDynSyms(const char *libPath, Fn &&visit)
        {
            FILE *fp = fopen(libPath, "rb");
            if (!fp)
                return;

            auto readAt = [&](void *dst, size_t size, long off) -> bool {
                if (0 != fseek(fp, off, SEEK_SET))
                    return false;
                return fread(dst, 1, size, fp) == size;
            };

#ifdef __LP64__
            using Ehdr = Elf64_Ehdr; using Shdr = Elf64_Shdr; using Sym = Elf64_Sym;
#else
            using Ehdr = Elf32_Ehdr; using Shdr = Elf32_Shdr; using Sym = Elf32_Sym;
#endif

            Ehdr ehdr{};
            if (!readAt(&ehdr, sizeof(ehdr), 0) ||
                0 != memcmp(ehdr.e_ident, ELFMAG, SELFMAG) ||
                sizeof(Shdr) != ehdr.e_shentsize || 0 == ehdr.e_shnum)
            {
                fclose(fp);
                return;
            }

            std::vector<Shdr> sections(ehdr.e_shnum);
            if (!readAt(sections.data(), sizeof(Shdr) * ehdr.e_shnum, static_cast<long>(ehdr.e_shoff)))
            {
                fclose(fp);
                return;
            }

            for (const auto &sh : sections)
            {
                if (SHT_DYNSYM != sh.sh_type || 0 == sh.sh_entsize)
                    continue;
                if (sh.sh_link >= sections.size())
                    continue;

                const Shdr &strtab = sections[sh.sh_link];

                std::string strbuf(strtab.sh_size, '\0');
                if (!readAt(strbuf.data(), strtab.sh_size, static_cast<long>(strtab.sh_offset)))
                    continue;

                std::vector<Sym> syms(sh.sh_size / sh.sh_entsize);
                if (!readAt(syms.data(), sh.sh_size, static_cast<long>(sh.sh_offset)))
                    continue;

                for (const auto &sym : syms)
                {
                    if (0 == sym.st_name || sym.st_name >= strbuf.size())
                        continue;
                    if (SHN_UNDEF == sym.st_shndx) // skip undefined imports
                        continue;

                    if (visit(strbuf.data() + sym.st_name))
                    {
                        fclose(fp);
                        return;
                    }
                }
            }

            fclose(fp);
        }

        // Return the first defined export whose mangled name contains `token`
        // and (if given) ends with `requiredSuffix`. The suffix lets callers
        // demand a specific ABI (parameter mangling) so we never bind an
        // overload we don't know how to call.
        inline std::string FindDynSymContaining(const char *libPath, const char *token,
                                                const char *requiredSuffix = nullptr)
        {
            std::string result;
            const size_t suffixLen = requiredSuffix ? strlen(requiredSuffix) : 0;

            EnumerateDynSyms(libPath, [&](const char *name) -> bool {
                if (!strstr(name, token))
                    return false;
                if (requiredSuffix)
                {
                    const size_t n = strlen(name);
                    if (n < suffixLen || 0 != strcmp(name + n - suffixLen, requiredSuffix))
                        return false;
                }
                result = name;
                return true;
            });

            return result;
        }

        struct Functionals
        {
            struct SymbolMethod
            {
                void *(*Open)(const char *filename, int flag) = nullptr;
                void *(*Find)(void *handle, const char *symbol) = nullptr;
                int (*Close)(void *handle) = nullptr;
            };

            size_t systemVersion = 13;
            // Resolved entry points remain valid only while these libraries stay
            // loaded. Keep the handles for this process-lifetime singleton.
            void* libguiHandle = nullptr;
            void* libutilsHandle = nullptr;

            template <typename Function>
            bool Require(Function function, const char* name) const {
                if (function) return true;
                std::fprintf(stderr, "[surface] Android %zu: required symbol unavailable: %s\n",
                             systemVersion, name);
                return false;
            }

            bool CanQueryDisplay() const {
                bool ready = Require(SurfaceComposerClient__Constructor, "SurfaceComposerClient constructor");
                ready = Require(RefBase__IncStrong, "RefBase::incStrong") && ready;
                if (systemVersion <= 9)
                    ready = Require(SurfaceComposerClient__GetBuiltInDisplay, "getBuiltInDisplay") && ready;
                else if (systemVersion < 14)
                    ready = Require(SurfaceComposerClient__GetInternalDisplayToken, "getInternalDisplayToken") && ready;
                else {
                    ready = Require(SurfaceComposerClient__GetPhysicalDisplayIds, "getPhysicalDisplayIds") && ready;
                    ready = Require(SurfaceComposerClient__GetPhysicalDisplayToken, "getPhysicalDisplayToken") && ready;
                }
                if (systemVersion >= 11)
                    ready = Require(SurfaceComposerClient__GetDisplayState, "getDisplayState") && ready;
                else
                    ready = Require(SurfaceComposerClient__GetDisplayInfo, "getDisplayInfo") && ready;
                return ready;
            }

            bool CanCreateSurface(bool skipScreenshot) const {
                bool ready = CanQueryDisplay();
                ready = Require(String8__Constructor, "String8 constructor") && ready;
                ready = Require(String8__Destructor, "String8 destructor") && ready;
                ready = Require(RefBase__DecStrong, "RefBase::decStrong") && ready;
                ready = Require(SurfaceControl__GetSurface, "SurfaceControl::getSurface") && ready;
                if (systemVersion >= 7)
                    ready = Require(SurfaceControl__DisConnect, "SurfaceControl::disconnect") && ready;
                if (systemVersion > 9)
                    ready = Require(LayerMetadata__Constructor, "LayerMetadata constructor") && ready;
                if (skipScreenshot && (systemVersion == 10 || systemVersion == 11))
                    ready = Require(LayerMetadata__setInt32, "LayerMetadata::setInt32") && ready;
                if (systemVersion == 8)
                    ready = Require(SurfaceComposerClient__CreateSurface_and8, "createSurface (Android 8)") && ready;
                else if (systemVersion == 9)
                    ready = Require(SurfaceComposerClient__CreateSurface_and9, "createSurface (Android 9)") && ready;
                else if (systemVersion >= 14)
                    ready = Require(SurfaceComposerClient__CreateSurface || SurfaceComposerClient__CreateSurface_constMetadata,
                                    "createSurface (value or const-reference metadata)") && ready;
                else
                    ready = Require(SurfaceComposerClient__CreateSurface, "createSurface") && ready;
                if (systemVersion >= 12) {
                    ready = Require(SurfaceComposerClient__Transaction__Constructor, "Transaction constructor") && ready;
                    ready = Require(SurfaceComposerClient__Transaction__SetTrustedOverlay, "Transaction::setTrustedOverlay") && ready;
                    ready = Require(SurfaceComposerClient__Transaction__SetLayer, "Transaction::setLayer") && ready;
                    ready = Require(SurfaceComposerClient__Transaction__Apply, "Transaction::apply") && ready;
                } else if (systemVersion <= 8) {
                    ready = Require(SurfaceComposerClient__OpenGlobalTransaction, "openGlobalTransaction") && ready;
                    ready = Require(SurfaceComposerClient__CloseGlobalTransaction, "closeGlobalTransaction") && ready;
                    ready = Require(SurfaceControl__SetLayer, "SurfaceControl::setLayer") && ready;
                }
                return ready;
            }

            void (*RefBase__IncStrong)(void *thiz, void *id) = nullptr;
            void (*RefBase__DecStrong)(void *thiz, void *id) = nullptr;

            void (*String8__Constructor)(void *thiz, const char *const data) = nullptr;
            void (*String8__Destructor)(void *thiz) = nullptr;

            void (*LayerMetadata__Constructor)(void *thiz) = nullptr;
            void (*LayerMetadata__setInt32)(void *thiz, uint32_t key, int32_t value) = nullptr;

            void (*SurfaceComposerClient__Constructor)(void *thiz) = nullptr;
            void (*SurfaceComposerClient__Destructor)(void *thiz) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, void *layerMetadata, uint32_t *outTransformHint) = nullptr;
            // AOSP a5756cdc7747253081338df64c81e122950fbaa3 changed metadata
            // from a value parameter to const LayerMetadata& (Android 17).
            // Bind the complete new mangling to its own prototype; never infer
            // a callable ABI from a substring match or just the OS version.
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_constMetadata)(void *thiz, const void *name, uint32_t w, uint32_t h, int32_t format, int32_t flags, const void *parentHandle, const void *layerMetadata, uint32_t *outTransformHint) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and8)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, uint32_t windowType, uint32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and9)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, int32_t windowType, int32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface)(void *thiz, void *mirrorFromSurface) = nullptr;
            // Known libgui versions add optional stopAt, then cropBy controls.
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface2)(void *thiz, void *mirrorFromSurface, void *stopAt) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface3)(void *thiz, void *mirrorFromSurface, void *stopAt, void *cropBy) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetInternalDisplayToken)() = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetBuiltInDisplay)(ui::DisplayType type) = nullptr;
            int32_t (*SurfaceComposerClient__GetDisplayState)(StrongPointer<void> &display, ui::DisplayState *displayState) = nullptr;
            int32_t (*SurfaceComposerClient__GetDisplayInfo)(StrongPointer<void> &display, ui::DisplayInfo *displayInfo) = nullptr;
            std::vector<ui::PhysicalDisplayId> (*SurfaceComposerClient__GetPhysicalDisplayIds)() = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetPhysicalDisplayToken)(ui::PhysicalDisplayId displayId) = nullptr;

            void (*SurfaceComposerClient__OpenGlobalTransaction)() = nullptr;
            void (*SurfaceComposerClient__CloseGlobalTransaction)(bool synchronous) = nullptr;

            void (*SurfaceComposerClient__Transaction__Constructor)(void *thiz) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetLayer)(void *thiz, StrongPointer<void> &surfaceControl, int32_t z) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetTrustedOverlay)(void *thiz, StrongPointer<void> &surfaceControl, bool isTrustedOverlay) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetLayerStack)(void *thiz, StrongPointer<void> &surfaceControl, uint32_t layerStack) = nullptr;
            // Android 12+. Tells SurfaceFlinger to blur whatever is composited
            // *behind* this layer, within its bounds — the same path the
            // system uses for notification-shade frosted glass. The blur runs
            // in the compositor, so it costs this process nothing per frame.
            void *(*SurfaceComposerClient__Transaction__SetBackgroundBlurRadius)(void *thiz, StrongPointer<void> &surfaceControl, int32_t radius) = nullptr;
            // Restricts a layer to a sub-rectangle, so a full-surface blur
            // layer can be confined to just the UI window's bounds.
            void *(*SurfaceComposerClient__Transaction__SetCrop)(void *thiz, StrongPointer<void> &surfaceControl, const ui::Rect *crop) = nullptr;

            // ── Virtual-display capture ──────────────────────────────────
            // Asking SurfaceFlinger to composite a layer stack into a Surface
            // we own is the only way to get the live screen as pixels we can
            // sample: GPU to GPU, no readback, at display refresh rate. These
            // are far older APIs than the blur path (they predate Android 5),
            // which is what makes this viable on old versions too.
            // Android 14 renamed createDisplay -> createVirtualDisplay and
            // swapped String8 for std::string (plus a refresh-rate arg), so
            // the two ABIs need separate pointers and the caller has to know
            // which one it bound.
            StrongPointer<void> (*SurfaceComposerClient__CreateDisplay)(void *name_String8, bool secure) = nullptr;
            // Separate complete signatures: neither a method-name substring
            // nor the trailing float distinguishes these incompatible ABIs.
            StrongPointer<void> (*SurfaceComposerClient__CreateVirtualDisplay_withoutPower)(const std::string *name, bool secure, const std::string *uniqueId, float refreshRate) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateVirtualDisplay)(const std::string *name, bool secure, bool optimizeForPower, const std::string *uniqueId, float refreshRate) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid)(const std::string *name, bool secure, bool optimizeForPower, const std::string *uniqueId, uint32_t ownerUid, float refreshRate) = nullptr;
            void (*SurfaceComposerClient__DestroyDisplay)(StrongPointer<void> &display) = nullptr;
            int32_t (*SurfaceComposerClient__DestroyVirtualDisplay)(StrongPointer<void> &display) = nullptr;
            int32_t (*SurfaceComposerClient__Transaction__SetDisplaySurface)(void *thiz, StrongPointer<void> &token, StrongPointer<void> &bufferProducer) = nullptr;
            void (*SurfaceComposerClient__Transaction__SetDisplayLayerStack)(void *thiz, StrongPointer<void> &token, ui::LayerStack layerStack) = nullptr;
            void (*SurfaceComposerClient__Transaction__SetDisplayLayerStack_legacy)(void *thiz, StrongPointer<void> &token, uint32_t layerStack) = nullptr;
            void (*SurfaceComposerClient__Transaction__SetDisplayProjection)(void *thiz, StrongPointer<void> &token, ui::Rotation orientation, const ui::Rect *layerStackRect, const ui::Rect *displayRect) = nullptr;
            void (*SurfaceComposerClient__Transaction__SetDisplayProjection_legacy)(void *thiz, StrongPointer<void> &token, uint32_t orientation, const ui::Rect *layerStackRect, const ui::Rect *displayRect) = nullptr;
            // ANativeWindow is an android::Surface; this hands back the
            // producer end to attach to the virtual display.
            StrongPointer<void> (*Surface__GetIGraphicBufferProducer)(void *thiz) = nullptr;
            // A display that is configured but powered off composites nothing,
            // which looks exactly like a layer-stack mismatch from outside.
            void (*SurfaceComposerClient__SetDisplayPowerMode)(StrongPointer<void> &display, int32_t mode) = nullptr;
            // Sets layer_state_t flags on a layer without recreating it.
            void *(*SurfaceComposerClient__Transaction__SetFlags)(void *thiz, StrongPointer<void> &surfaceControl, uint32_t flags, uint32_t mask) = nullptr;
            // Android 13+. Returns a SurfaceControl that mirrors a display's
            // contents, to be parented onto whichever layer stack should show
            // it. This is how modern SurfaceFlinger mirrors, having moved away
            // from two displays simply sharing one layer stack.
            //
            // Non-static despite appearances: it issues its binder call
            // through the client's own mClient member, so `this` comes first.
            // C++ manglings do not encode staticness, so the symbol name for a
            // static and a member overload is identical and gives no hint —
            // calling it without `this` puts displayId where the object should
            // be and segfaults on the first member access.
            StrongPointer<void> (*SurfaceComposerClient__MirrorDisplay)(void *thiz, ui::PhysicalDisplayId displayId) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Show)(void *thiz, StrongPointer<void> &surfaceControl) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Hide)(void *thiz, StrongPointer<void> &surfaceControl) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Reparent)(void *thiz, StrongPointer<void> &surfaceControl, StrongPointer<void> &newParentHandle) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetMatrix)(void *thiz, StrongPointer<void> &surfaceControl, float dsdx, float dtdx, float dtdy, float dsdy) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetPosition)(void *thiz, StrongPointer<void> &surfaceControl, float x, float y) = nullptr;
            int32_t (*SurfaceComposerClient__Transaction__Apply)(void *thiz, bool synchronous, bool oneWay) = nullptr;

            int32_t (*SurfaceControl__Validate)(void *thiz) = nullptr;
            StrongPointer<Surface> (*SurfaceControl__GetSurface)(void *thiz) = nullptr;
            void (*SurfaceControl__DisConnect)(void *thiz) = nullptr;
            void *(*SurfaceControl__SetLayer)(void *thiz, int32_t z) = nullptr;
            
            // Surface related methods
            void (*Surface__DisConnect)(void *thiz, int32_t api) = nullptr;

            Functionals(const SymbolMethod &symbolMethod)
            {
                std::string systemVersionString(128, 0);

                systemVersionString.resize(__system_property_get("ro.build.version.release", systemVersionString.data()));
                if (!systemVersionString.empty())
                    systemVersion = std::stoi(systemVersionString);

                if (5 > systemVersion)
                {
                    SURFACE_LOG_ERROR("Unsupported system version: %zu", systemVersion);
                    return;
                }

#ifdef __LP64__
                const char *libguiPath = "/system/lib64/libgui.so";
                auto libgui = symbolMethod.Open(libguiPath, RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib64/libutils.so", RTLD_LAZY);
#else
                const char *libguiPath = "/system/lib/libgui.so";
                auto libgui = symbolMethod.Open(libguiPath, RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib/libutils.so", RTLD_LAZY);
#endif
                libguiHandle = libgui;
                libutilsHandle = libutils;
                if (!libgui || !libutils) {
                    std::fprintf(stderr, "[surface] Android %zu: cannot load %s%s%s\n", systemVersion,
                                 libgui ? "" : "libgui.so", (!libgui && !libutils) ? " and " : "",
                                 libutils ? "" : "libutils.so");
                    return;
                }
                //libutils
                ResolveMethod(RefBase, IncStrong, libutils, "_ZNK7android7RefBase9incStrongEPKv");
                ResolveMethod(RefBase, DecStrong, libutils, "_ZNK7android7RefBase9decStrongEPKv");

                ResolveMethod(String8, Constructor, libutils, "_ZN7android7String8C2EPKc");
                ResolveMethod(String8, Destructor, libutils, "_ZN7android7String8D2Ev");
                
                //libgui
                if (10 <= systemVersion && 13 >= systemVersion) {
                    ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android13LayerMetadataC2Ev");
                    ResolveMethod(LayerMetadata, setInt32, libgui, "_ZN7android13LayerMetadata8setInt32Eji");
                } else if (14 <= systemVersion) {
                    ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android3gui13LayerMetadataC2Ev");
                }

                ResolveMethod(SurfaceComposerClient, Constructor, libgui, "_ZN7android21SurfaceComposerClientC2Ev");

                // Select the correct CreateSurface API based on Android version
                if (5 <= systemVersion && 7 >= systemVersion) {
                    // Android 5-7
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8Ejjij");
                } else if (8 == systemVersion) {
                    // Android 8
                    ResolveMethod(SurfaceComposerClient, CreateSurface_and8, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEjj");
                } else if (9 == systemVersion) {
                    // Android 9
                    ResolveMethod(SurfaceComposerClient, CreateSurface_and9, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii");
                } else if (10 == systemVersion) {
                    // Android 10
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE");
                } else if (11 == systemVersion) {
                    // Android 11
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj");
                } else if (12 <= systemVersion && 13 >= systemVersion) {
                    // Android 12-13
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj");
                } else if (14 <= systemVersion) {
                    // Android 14+
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
                    if (!SurfaceComposerClient__CreateSurface) {
                        ResolveMethod(SurfaceComposerClient, CreateSurface_constMetadata, libgui,
                                      "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj");
                    }
                    if (!SurfaceComposerClient__CreateSurface && !SurfaceComposerClient__CreateSurface_constMetadata) {
                        // Diagnostics only: unknown exports must never be called.
                        int reported = 0;
                        EnumerateDynSyms(libguiPath, [&](const char* name) {
                            if (std::strstr(name, "SurfaceComposerClient13createSurfaceE")) {
                                std::fprintf(stderr, "[surface] unsupported createSurface export: %s\n", name);
                                return ++reported >= 8;
                            }
                            return false;
                        });
                    }
                }
                
                // MirrorSurface method - Android 11+
                //
                // These are non-static members with one, two or three explicit
                // SurfaceControl* arguments. Newer Android 17 adds cropBy after
                // stopAt. Resolve complete signatures, then pass nullptr for
                // both optional controls; never infer an ABI from the OS version
                // or a partial symbol name.
                if (11 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient, MirrorSurface, libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlE");
                    SurfaceComposerClient__MirrorSurface2 = reinterpret_cast<decltype(SurfaceComposerClient__MirrorSurface2)>(
                        symbolMethod.Find(libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_"));
                    SurfaceComposerClient__MirrorSurface3 = reinterpret_cast<decltype(SurfaceComposerClient__MirrorSurface3)>(
                        symbolMethod.Find(libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_S2_"));

                    if (nullptr == SurfaceComposerClient__MirrorSurface &&
                        nullptr == SurfaceComposerClient__MirrorSurface2 &&
                        nullptr == SurfaceComposerClient__MirrorSurface3) {
                        SURFACE_LOG_WARN("No callable mirrorSurface on this ROM; recordings won't capture overlay (no crash)");
                    } else {
                        SURFACE_LOG_INFO("mirrorSurface resolved (1-arg=%p 2-arg=%p 3-arg=%p)",
                                         (void *)SurfaceComposerClient__MirrorSurface,
                                         (void *)SurfaceComposerClient__MirrorSurface2,
                                         (void *)SurfaceComposerClient__MirrorSurface3);
                    }
                }
                
                // Resolve complete, known signatures only. Android 17 exports
                // both Surface and IGraphicBufferProducer setDisplaySurface
                // overloads; their object pointers are not interchangeable.
                {
                    const bool symDebug = nullptr != getenv("AIMGUI_SYM_DEBUG");
                    auto exact = [&](auto& slot, const char* name) {
                        using Fn = std::remove_reference_t<decltype(slot)>;
                        slot = reinterpret_cast<Fn>(symbolMethod.Find(libgui, name));
                        if (symDebug && slot) fprintf(stderr, "[sym] %s\n", name);
                    };
                    exact(SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid,
                          "_ZN7android21SurfaceComposerClient20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbbS9_jf");
                    exact(SurfaceComposerClient__CreateVirtualDisplay,
                          "_ZN7android21SurfaceComposerClient20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbbS9_f");
                    exact(SurfaceComposerClient__CreateVirtualDisplay_withoutPower,
                          "_ZN7android21SurfaceComposerClient20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbS9_f");
                    exact(SurfaceComposerClient__CreateDisplay,
                          "_ZN7android21SurfaceComposerClient13createDisplayERKNS_7String8Eb");
                    exact(SurfaceComposerClient__DestroyVirtualDisplay,
                          "_ZN7android21SurfaceComposerClient21destroyVirtualDisplayERKNS_2spINS_7IBinderEEE");
                    exact(SurfaceComposerClient__DestroyDisplay,
                          "_ZN7android21SurfaceComposerClient14destroyDisplayERKNS_2spINS_7IBinderEEE");
                    exact(SurfaceComposerClient__Transaction__SetDisplaySurface,
                          "_ZN7android21SurfaceComposerClient11Transaction17setDisplaySurfaceERKNS_2spINS_7IBinderEEERKNS2_INS_22IGraphicBufferProducerEEE");
                    exact(SurfaceComposerClient__Transaction__SetDisplayLayerStack,
                          "_ZN7android21SurfaceComposerClient11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEENS_2ui10LayerStackE");
                    exact(SurfaceComposerClient__Transaction__SetDisplayLayerStack_legacy,
                          "_ZN7android21SurfaceComposerClient11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEEj");
                    exact(SurfaceComposerClient__Transaction__SetDisplayProjection,
                          "_ZN7android21SurfaceComposerClient11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEENS_2ui8RotationERKNS_4RectESB_");
                    exact(SurfaceComposerClient__Transaction__SetDisplayProjection_legacy,
                          "_ZN7android21SurfaceComposerClient11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEEjRKNS_4RectES9_");
                    exact(Surface__GetIGraphicBufferProducer,
                          "_ZNK7android7Surface25getIGraphicBufferProducerEv");
                    exact(SurfaceComposerClient__SetDisplayPowerMode,
                          "_ZN7android21SurfaceComposerClient19setDisplayPowerModeERKNS_2spINS_7IBinderEEEi");
                    exact(SurfaceComposerClient__Transaction__SetFlags,
                          "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj");
                    exact(SurfaceComposerClient__MirrorDisplay,
                          "_ZN7android21SurfaceComposerClient13mirrorDisplayENS_9DisplayIdE");
                }

                // Display related methods - version specific selection
                if (5 <= systemVersion && 9 >= systemVersion) {
                    // Android 5-9 uses GetBuiltInDisplay
                    ResolveMethod(SurfaceComposerClient, GetBuiltInDisplay, libgui, "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi");
                }
                if (10 <= systemVersion && 13 >= systemVersion) {
                    // Android 10-13 uses GetInternalDisplayToken
                    ResolveMethod(SurfaceComposerClient, GetInternalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
                }
                if (10 <= systemVersion) {
                    // Android 10+ uses GetPhysicalDisplayIds
                    ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayIds, libgui, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
                }
                if (12 <= systemVersion) {
                    // Android 12+ uses GetPhysicalDisplayToken
                    ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
                }
                
                // Display state and info retrieval methods
                if (5 <= systemVersion && 11 >= systemVersion) {
                    // Android 5-11 uses GetDisplayInfo
                    ResolveMethod(SurfaceComposerClient, GetDisplayInfo, libgui, "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE");
                }
                if (11 <= systemVersion) {
                    // Android 11+ uses GetDisplayState
                    ResolveMethod(SurfaceComposerClient, GetDisplayState, libgui, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
                }

                // GlobalTransaction methods - Android 5-8 only
                if (5 <= systemVersion && 8 >= systemVersion) {
                    ResolveMethod(SurfaceComposerClient, OpenGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv");
                    ResolveMethod(SurfaceComposerClient, CloseGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb");
                }

                // Transaction related methods - Android 9+
                if (12 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, Constructor, libgui, "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
                }
                if (9 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetLayer, libgui, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
                    ResolveMethod(SurfaceComposerClient__Transaction, Show, libgui, "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE");
                    ResolveMethod(SurfaceComposerClient__Transaction, Hide, libgui, "_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE");
                }
                if (12 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetTrustedOverlay, libgui, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
                    ResolveMethod(SurfaceComposerClient__Transaction, Reparent, libgui, "_ZN7android21SurfaceComposerClient11Transaction8reparentERKNS_2spINS_14SurfaceControlEEES6_");
                    // Background blur (Android 12+). Absent on ROMs built
                    // without it; callers must null-check before use.
                    ResolveMethod(SurfaceComposerClient__Transaction, SetBackgroundBlurRadius, libgui, "_ZN7android21SurfaceComposerClient11Transaction23setBackgroundBlurRadiusERKNS_2spINS_14SurfaceControlEEEi");
                    ResolveMethod(SurfaceComposerClient__Transaction, SetCrop, libgui, "_ZN7android21SurfaceComposerClient11Transaction7setCropERKNS_2spINS_14SurfaceControlEEERKNS_4RectE");
                }
                if (9 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetMatrix, libgui, "_ZN7android21SurfaceComposerClient11Transaction9setMatrixERKNS_2spINS_14SurfaceControlEEEffff");
                }
                if (5 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetPosition, libgui, "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff");
                }
                if (13 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetLayerStack, libgui, "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE");
                }
                
                // Transaction Apply method - version specific selection
                if (9 <= systemVersion && 12 >= systemVersion) {
                    // Android 9-12 uses two-parameter version
                    ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEb");
                }
                if (13 <= systemVersion) {
                    // Android 13+ uses three-parameter version
                    ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
                }

                // SurfaceControl related methods
                if (5 <= systemVersion) {
                    ResolveMethod(SurfaceControl, Validate, libgui, "_ZNK7android14SurfaceControl8validateEv");
                }
                
                // SurfaceControl GetSurface method - version specific selection
                if (5 <= systemVersion && 11 >= systemVersion) {
                    // Android 5-11 uses const version
                    ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZNK7android14SurfaceControl10getSurfaceEv");
                }
                if (12 <= systemVersion) {
                    // Android 12+ uses non-const version
                    ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZN7android14SurfaceControl10getSurfaceEv");
                }
                
                // DisConnect method - version specific selection
                if (5 <= systemVersion && 6 >= systemVersion) {
                    // Android 5-6 uses Surface::disconnect
                    ResolveMethod(Surface, DisConnect, libgui, "_ZN7android7Surface10disconnectEi");
                }
                if (7 <= systemVersion) {
                    // Android 7+ uses SurfaceControl::disconnect
                    ResolveMethod(SurfaceControl, DisConnect, libgui, "_ZN7android14SurfaceControl10disconnectEv");
                }
                
                // SetLayer method - version specific selection
                if (5 == systemVersion || 8 == systemVersion) {
                    // Android 5 and 8+ use int version
                    ResolveMethod(SurfaceControl, SetLayer, libgui, "_ZN7android14SurfaceControl8setLayerEi");
                }
                if (6 <= systemVersion && 7 >= systemVersion) {
                    // Android 6-7 use uint version
                    ResolveMethod(SurfaceControl, SetLayer, libgui, "_ZN7android14SurfaceControl8setLayerEj");
                }

                // Do not dlclose here: all stored function pointers and the
                // SurfaceFlinger objects created through them outlive this call.
            }

            static const Functionals &GetInstance(const SymbolMethod &symbolMethod = {.Open = dlopen, .Find = dlsym, .Close = dlclose}) {
                static Functionals functionals(symbolMethod);
                return functionals;
            }
        };

        struct String8
        {
            char data[1024];

            String8(const char *const string)
            {
                Functionals::GetInstance().String8__Constructor(data, string);
            }

            ~String8()
            {
                Functionals::GetInstance().String8__Destructor(data);
            }

            operator void *()
            {
                return reinterpret_cast<void *>(data);
            }
        };

        struct LayerMetadata {
            char data[1024];

            LayerMetadata() {
                if (9 < Functionals::GetInstance().systemVersion) {
                    Functionals::GetInstance().LayerMetadata__Constructor(data);
                }
            }
            
            void setInt32(uint32_t key, int32_t value) {
                Functionals::GetInstance().LayerMetadata__setInt32(data, key, value);            
            }
            
            operator void *() {
                if (9 < Functionals::GetInstance().systemVersion)
                    return reinterpret_cast<void *>(data);
                else
                    return nullptr;
            }
        };

        struct Surface {
        };

        struct SurfaceControl {
            void *data;
            // Recording mirrors own both their clone and its root. Copies in
            // the display cache share this lifetime; ordinary windows retain
            // their existing explicit DestroySurface lifecycle.
            std::shared_ptr<void> mirrorOwnership;

            SurfaceControl() : data(nullptr) {}
            SurfaceControl(void *data) : data(data) {}

            int32_t Validate() {
                if (nullptr == data)
                    return 0;

                return Functionals::GetInstance().SurfaceControl__Validate(data);
            }

            Surface *GetSurface() {
                if (nullptr == data)
                    return nullptr;

                const auto getSurface = Functionals::GetInstance().SurfaceControl__GetSurface;
                if (!getSurface) return nullptr;
                SURFACE_STARTUP_TRACE("calling SurfaceControl::getSurface");
                auto result = getSurface(data);
                if (!result.pointer) return nullptr;

                return reinterpret_cast<Surface *>(reinterpret_cast<size_t>(result.pointer) + sizeof(std::max_align_t) / 2);
            }

            void DisConnect() {
                if (nullptr == data)
                    return;

                Functionals::GetInstance().SurfaceControl__DisConnect(data);
            }

            void SetLayer(int32_t z) {
                if (nullptr == data)
                    return;

                Functionals::GetInstance().SurfaceControl__SetLayer(data, z);
            }

            void DestroySurface(Surface *surface) {
                if (nullptr == data || nullptr == surface)
                    return;

                Functionals::GetInstance().RefBase__DecStrong(reinterpret_cast<Surface *>(reinterpret_cast<size_t>(surface) - sizeof(std::max_align_t) / 2), this);
                DisConnect();
                Functionals::GetInstance().RefBase__DecStrong(data, this);
            }
        };

        struct SurfaceComposerClientTransaction {
            char data[1024];

            SurfaceComposerClientTransaction() {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Constructor(data);
            }

            void *SetLayer(StrongPointer<void> &surfaceControl, int32_t z) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayer(data, surfaceControl, z);
            }

            void *SetTrustedOverlay(StrongPointer<void> &surfaceControl, bool isTrustedOverlay) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetTrustedOverlay(data, surfaceControl, isTrustedOverlay);
            }

            void *SetLayerStack(StrongPointer<void> &surfaceControl, uint32_t layerStack) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayerStack(data, surfaceControl, layerStack);
            }

            // Both of these are Android 12+ and can be missing entirely on
            // ROMs whose libgui was built without them, so they return false
            // rather than jumping through a null pointer — the mistake that
            // made mirrorSurface segfault the process.
            bool SetBackgroundBlurRadius(StrongPointer<void> &surfaceControl, int32_t radius) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__Transaction__SetBackgroundBlurRadius;
                if (nullptr == fn) return false;
                fn(data, surfaceControl, radius);
                return true;
            }

            bool SetCrop(StrongPointer<void> &surfaceControl, const ui::Rect &crop) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__Transaction__SetCrop;
                if (nullptr == fn) return false;
                fn(data, surfaceControl, &crop);
                return true;
            }

            bool SetFlags(StrongPointer<void> &surfaceControl, uint32_t flags, uint32_t mask) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__Transaction__SetFlags;
                if (nullptr == fn) return false;
                fn(data, surfaceControl, flags, mask);
                return true;
            }

            // ── Virtual display ─────────────────────────────────────────
            // Attaching a display to a producer we own is what makes
            // SurfaceFlinger composite the screen into our buffers.
            bool SetDisplaySurface(StrongPointer<void> &token, StrongPointer<void> &producer) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__Transaction__SetDisplaySurface;
                if (nullptr == fn) return false;
                return fn(data, token, producer) == 0;
            }

            bool SetDisplayLayerStack(StrongPointer<void> &token, uint32_t layerStack) {
                const auto& f = Functionals::GetInstance();
                if (f.SurfaceComposerClient__Transaction__SetDisplayLayerStack) {
                    f.SurfaceComposerClient__Transaction__SetDisplayLayerStack(data, token, ui::LayerStack{layerStack});
                    return true;
                }
                if (f.SurfaceComposerClient__Transaction__SetDisplayLayerStack_legacy) {
                    f.SurfaceComposerClient__Transaction__SetDisplayLayerStack_legacy(data, token, layerStack);
                    return true;
                }
                return false;
            }

            // `layerStackRect` is the region of the source display to read,
            // `displayRect` where it lands in our virtual display.
            bool SetDisplayProjection(StrongPointer<void> &token, int32_t orientation,
                                      const ui::Rect &layerStackRect, const ui::Rect &displayRect) {
                const auto& f = Functionals::GetInstance();
                if (f.SurfaceComposerClient__Transaction__SetDisplayProjection) {
                    f.SurfaceComposerClient__Transaction__SetDisplayProjection(data, token,
                        static_cast<ui::Rotation>(orientation), &layerStackRect, &displayRect);
                    return true;
                }
                if (f.SurfaceComposerClient__Transaction__SetDisplayProjection_legacy) {
                    f.SurfaceComposerClient__Transaction__SetDisplayProjection_legacy(data, token,
                        static_cast<uint32_t>(orientation), &layerStackRect, &displayRect);
                    return true;
                }
                return false;
            }

            static bool BackgroundBlurSupported() {
                return nullptr != Functionals::GetInstance().SurfaceComposerClient__Transaction__SetBackgroundBlurRadius
                    && nullptr != Functionals::GetInstance().SurfaceComposerClient__Transaction__SetCrop;
            }

            void Show(StrongPointer<void> &surfaceControl) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Show(data, surfaceControl);
            }

            void Hide(StrongPointer<void> &surfaceControl) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Hide(data, surfaceControl);
            }

            void Reparent(StrongPointer<void> &surfaceControl, StrongPointer<void> &newParentHandle) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Reparent(data, surfaceControl, newParentHandle);
            }

            void *SetMatrix(StrongPointer<void> &surfaceControl, float dsdx, float dtdx, float dtdy, float dsdy) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetMatrix(data, surfaceControl, dsdx, dtdx, dtdy, dsdy);
            }

            void SetPosition(StrongPointer<void> &surfaceControl, float x, float y) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__SetPosition(data, surfaceControl, x, y);
            }

            int32_t Apply(bool synchronous, bool oneWay) {
                if (12 >= Functionals::GetInstance().systemVersion)
                    return reinterpret_cast<int32_t (*)(void *, bool)>(Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply)(data, synchronous);
                else
                    return Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply(data, synchronous, oneWay);
            }
        };

        struct SurfaceComposerClient {
            char data[1024];
            bool ready = false;

            SurfaceComposerClient() {
                const auto& functions = Functionals::GetInstance();
                if (!functions.CanQueryDisplay()) return;
                SURFACE_STARTUP_TRACE("calling SurfaceComposerClient constructor");
                functions.SurfaceComposerClient__Constructor(data);
                SURFACE_STARTUP_TRACE("calling RefBase::incStrong");
                functions.RefBase__IncStrong(data, this);
                ready = true;
            }

            SurfaceControl CreateSurface(const char *name, int32_t width, int32_t height, uint32_t windowFlags = 0, bool skipScrenshot = false) {
                if (!ready || !Functionals::GetInstance().CanCreateSurface(skipScrenshot)) return {};
                if (width <= 0 || height <= 0) {
                    std::fprintf(stderr, "[surface] invalid surface dimensions: %d x %d\n", width, height);
                    return {};
                }
                static void *parentHandle = nullptr;
                parentHandle = nullptr;
                
                SURFACE_STARTUP_TRACE("constructing String8 and LayerMetadata");
                String8 windowName(name);
                int32_t pixelFormat = 1; // RGBA_8888
                LayerMetadata layerMetadata{};
                auto systemVersion = Functionals::GetInstance().systemVersion;

                StrongPointer<void> result{};
                
                SURFACE_STARTUP_TRACE("calling createSurface");
                switch (systemVersion) {
                case 5:
                case 6:
                case 7:
                {
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 8:
                {
                    uint32_t windowType = 0;
                    uint32_t ownerUid = 0;
                    if (skipScrenshot) {
                        windowType = 441731;
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface_and8(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, windowType, ownerUid);
                    break;
                }
                case 9:
                {
                    int32_t windowType = -1;
                    int32_t ownerUid = -1;
                    if (skipScrenshot) {
                        windowType = 441731;
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface_and9(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, windowType, ownerUid);
                    break;
                }
                case 10:
                {
                    if (skipScrenshot) {
                        layerMetadata.setInt32(2u, 441731);
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 11:
                {
                    if (skipScrenshot) {
                        layerMetadata.setInt32(2u, 441731);
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 12:
                case 13:
                {
                    if (skipScrenshot) {
                        windowFlags |= 0x40; // eSkipScreenshot
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, &parentHandle, layerMetadata, nullptr);
                    break;
                }
                default: // Android 14+, selected by the exact exported signature.
                {
                    if (skipScrenshot) {
                        windowFlags |= 0x40; // eSkipScreenshot
                    }
                    const auto& functions = Functionals::GetInstance();
                    if (functions.SurfaceComposerClient__CreateSurface) {
                        SURFACE_STARTUP_TRACE("createSurface ABI: value LayerMetadata");
                        result = functions.SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, &parentHandle, layerMetadata, nullptr);
                    } else {
                        SURFACE_STARTUP_TRACE("createSurface ABI: const LayerMetadata&");
                        result = functions.SurfaceComposerClient__CreateSurface_constMetadata(data, windowName, width, height, pixelFormat, static_cast<int32_t>(windowFlags), &parentHandle, layerMetadata, nullptr);
                    }
                    break;
                }
                }

                // Check if Surface creation was successful
                if (nullptr == result.get()) {
                    SURFACE_LOG_ERROR("Failed to create surface: %s", name);
                    std::fprintf(stderr, "[surface] createSurface returned no SurfaceControl\n");
                    return {};
                }

                // Apply permission fixes
                if (12 <= systemVersion) {
                    // Android 12+: Use Transaction mechanism to set trusted overlay and highest layer.
                    // Preserve the input compatibility of this system overlay.
                    // Trusted-overlay input policy is separate from capture
                    // visibility; recording inclusion is handled by the skip
                    // flag and, where necessary, an explicit recording mirror.
                    SURFACE_STARTUP_TRACE("surface created; applying overlay transaction");
                    static SurfaceComposerClientTransaction transaction;
                    transaction.SetTrustedOverlay(result, true);
                    transaction.SetLayer(result, INT_MAX);
                    auto applyResult = transaction.Apply(false, true);
                } else if (8 >= systemVersion) {
                    // Android 8 and below: Use global transaction to set layer
                    OpenGlobalTransaction();
                    SurfaceControl{result.get()}.SetLayer(INT_MAX);
                    CloseGlobalTransaction(false);
                }

                return {result.get()};
            }

            // Same query, but for a caller-supplied display token instead of
            // the built-in one — lets a virtual display's stored state be read
            // back and compared against what was sent.
            // Mirrors the given physical display into a fresh layer. Empty if
            // the symbol is absent (pre-Android 13).
            SurfaceControl MirrorDisplay(ui::PhysicalDisplayId id) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__MirrorDisplay;
                if (nullptr == fn) return {};
                return SurfaceControl{fn(data, id).get()};
            }

            static bool MirrorDisplaySupported() {
                return nullptr != Functionals::GetInstance().SurfaceComposerClient__MirrorDisplay;
            }

            // mode: 0 = off, 1 = doze, 2 = on.
            bool SetDisplayPowerMode(StrongPointer<void> &token, int32_t mode) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__SetDisplayPowerMode;
                if (nullptr == fn) return false;
                fn(token, mode);
                return true;
            }

            bool GetDisplayStateOf(StrongPointer<void> &token, ui::DisplayState *out) {
                auto fn = Functionals::GetInstance().SurfaceComposerClient__GetDisplayState;
                if (nullptr == fn) return false;
                return 0 == fn(token, out);
            }

            bool GetDisplayInfo(ui::DisplayState *displayInfo) {
                if (!ready || !displayInfo) return false;
                static StrongPointer<void> defaultDisplay;

                if (nullptr == defaultDisplay.get()) {
                    if (9 >= Functionals::GetInstance().systemVersion) { // Android 9 and below
                        defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetBuiltInDisplay(ui::DisplayType::DisplayIdMain);
                    } else {
                        if (14 > Functionals::GetInstance().systemVersion) { // Android 10-13
                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetInternalDisplayToken();
                        } else { // Android 14 and above
                            SURFACE_STARTUP_TRACE("calling getPhysicalDisplayIds");
                            auto displayIds = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayIds();
                            if (displayIds.empty())
                                return false;

                            SURFACE_STARTUP_TRACE("calling getPhysicalDisplayToken");
                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayToken(displayIds[0]);
                        }
                    }
                }

                if (nullptr == defaultDisplay.get())
                    return false;

                if (11 <= Functionals::GetInstance().systemVersion) { // Android 11 and above
                    static bool firstQuery = true;
                    if (firstQuery) { SURFACE_STARTUP_TRACE("calling getDisplayState"); firstQuery = false; }
                    return 0 == Functionals::GetInstance().SurfaceComposerClient__GetDisplayState(defaultDisplay, displayInfo);
                } else { // Android 10 and below
                    ui::DisplayInfo realDisplayInfo{};
                    if (0 != Functionals::GetInstance().SurfaceComposerClient__GetDisplayInfo(defaultDisplay, &realDisplayInfo))
                        return false;

                    displayInfo->layerStackSpaceRect.width = realDisplayInfo.w;
                    displayInfo->layerStackSpaceRect.height = realDisplayInfo.h;
                    displayInfo->orientation = static_cast<ui::Rotation>(realDisplayInfo.orientation);

                    return true;
                }
            }

            void OpenGlobalTransaction() {
                Functionals::GetInstance().SurfaceComposerClient__OpenGlobalTransaction();
            }

            void CloseGlobalTransaction(bool synchronous) {
                Functionals::GetInstance().SurfaceComposerClient__CloseGlobalTransaction(synchronous);
            }

            // Creates a virtual display. Android 14 renamed this and switched
            // String8 for std::string, so both ABIs are handled here and the
            // caller just gets a token back.
            StrongPointer<void> CreateVirtualDisplay(const char *name, bool secure) {
                const auto &f = Functionals::GetInstance();
                if (f.SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid ||
                    f.SurfaceComposerClient__CreateVirtualDisplay ||
                    f.SurfaceComposerClient__CreateVirtualDisplay_withoutPower) {
                    const std::string n(name);
                    // Non-empty: SurfaceFlinger keys displays by uniqueId, and
                    // AOSP's own screenrecord always passes one. An empty id
                    // still yields a display that shows up in dumpsys, so this
                    // is not something the setup calls report on.
                    const std::string uniqueId(name);
                    if (f.SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid) {
                        SURFACE_STARTUP_TRACE("createVirtualDisplay ABI: 6 args (ownerUid)");
                        // gui::Uid::INVALID, the upstream default.
                        return f.SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid(&n, secure, false, &uniqueId, UINT32_MAX, 0.0f);
                    }
                    if (f.SurfaceComposerClient__CreateVirtualDisplay) {
                        SURFACE_STARTUP_TRACE("createVirtualDisplay ABI: 5 args (optimizeForPower)");
                        return f.SurfaceComposerClient__CreateVirtualDisplay(&n, secure, false, &uniqueId, 0.0f);
                    }
                    SURFACE_STARTUP_TRACE("createVirtualDisplay ABI: 4 args");
                    return f.SurfaceComposerClient__CreateVirtualDisplay_withoutPower(&n, secure, &uniqueId, 0.0f);
                }
                if (f.SurfaceComposerClient__CreateDisplay) {
                    SURFACE_STARTUP_TRACE("createDisplay ABI: String8, bool");
                    String8 n(name);
                    return f.SurfaceComposerClient__CreateDisplay(n, secure);
                }
                return {};
            }

            void DestroyVirtualDisplay(StrongPointer<void> &token) {
                const auto &f = Functionals::GetInstance();
                if (f.SurfaceComposerClient__DestroyVirtualDisplay) f.SurfaceComposerClient__DestroyVirtualDisplay(token);
                else if (f.SurfaceComposerClient__DestroyDisplay) f.SurfaceComposerClient__DestroyDisplay(token);
            }

            SurfaceControl MirrorSurface(SurfaceControl &surface, uint32_t layerStack) {
                using mirror_surfaces_t = std::pair<void *, void *>;
                constexpr auto MirrorSurfacesDeleter = [](mirror_surfaces_t *pair) {
                    SurfaceControl fakeSurface;

                    // Clean up mirror surface
                    if (pair->first) {
                        Functionals::GetInstance().SurfaceControl__DisConnect(pair->first);
                        Functionals::GetInstance().RefBase__DecStrong(pair->first, fakeSurface.data);
                    }

                    // Clean up mirror root surface
                    if (pair->second) {
                        Functionals::GetInstance().SurfaceControl__DisConnect(pair->second);
                        Functionals::GetInstance().RefBase__DecStrong(pair->second, fakeSurface.data);
                    }

                    delete pair;
                };

                if (13 > Functionals::GetInstance().systemVersion) {
                    return {};
                }

                // The mirrorSurface symbol can fail to resolve on non-AOSP ROMs
                // (different libgui build / renamed symbol). Calling a NULL
                // fn-ptr was segfaulting the process the moment the system
                // screen recorder added its VirtualDisplay layerStack. Pick the
                // ABI that resolved, passing null optional stopAt/cropBy controls.
                const auto &fn = Functionals::GetInstance();
                if (!fn.SurfaceComposerClient__Transaction__SetLayerStack ||
                    !fn.SurfaceComposerClient__Transaction__Show ||
                    !fn.SurfaceComposerClient__Transaction__Reparent ||
                    !fn.SurfaceComposerClient__Transaction__SetMatrix ||
                    !fn.SurfaceComposerClient__Transaction__SetPosition ||
                    !fn.SurfaceControl__DisConnect || !fn.RefBase__DecStrong) {
                    std::fprintf(stderr, "[recording] required mirror transaction/cleanup interface unavailable\n");
                    return {};
                }
                StrongPointer<void> mirrorSurface{};
                if (nullptr != fn.SurfaceComposerClient__MirrorSurface) {
                    mirrorSurface = fn.SurfaceComposerClient__MirrorSurface(data, surface.data);
                } else if (nullptr != fn.SurfaceComposerClient__MirrorSurface2) {
                    mirrorSurface = fn.SurfaceComposerClient__MirrorSurface2(data, surface.data, nullptr);
                } else if (nullptr != fn.SurfaceComposerClient__MirrorSurface3) {
                    mirrorSurface = fn.SurfaceComposerClient__MirrorSurface3(data, surface.data, nullptr, nullptr);
                } else {
                    std::fprintf(stderr, "[recording] mirrorSurface unavailable on Android %zu\n", fn.systemVersion);
                    return {};
                }
                if (nullptr == mirrorSurface.get()) {
                    std::fprintf(stderr, "[recording] mirrorSurface returned no layer for stack %u\n", layerStack);
                    return {};
                }
                auto ownership = std::shared_ptr<mirror_surfaces_t>(
                    new mirror_surfaces_t{mirrorSurface.get(), nullptr}, MirrorSurfacesDeleter);

                // Get display dimensions
                int32_t width = -1, height = -1;
                while (-1 == width || -1 == height) {
                    ui::DisplayState displayInfo{};
                    if (!GetDisplayInfo(&displayInfo))
                        break;

                    width = displayInfo.layerStackSpaceRect.width;
                    height = displayInfo.layerStackSpaceRect.height;
                    break;
                }

                SURFACE_LOG_INFO("Mirror surface size: %d x %d", width, height);

                // Create mirror root surface
                auto mirrorRootName = "MirrorRoot@" + std::to_string(layerStack);
                auto mirrorRootSurface = CreateSurface(mirrorRootName.c_str(), width, height, 0x00004000);
                if (!mirrorRootSurface.data) {
                    return {};
                }
                ownership->second = mirrorRootSurface.data;

                // Set mirror root surface properties
                static SurfaceComposerClientTransaction transaction;
                
                StrongPointer<void> mirrorRootPtr{mirrorRootSurface.data};
                StrongPointer<void> mirrorPtr{mirrorSurface.get()};
                
                transaction.SetLayer(mirrorRootPtr, INT_MAX);
                transaction.SetLayerStack(mirrorRootPtr, layerStack);

                // Set mirror surface properties
                transaction.SetLayerStack(mirrorPtr, layerStack);
                transaction.Show(mirrorPtr);
                transaction.Reparent(mirrorPtr, mirrorRootPtr);
                const int32_t result = transaction.Apply(false, false);
                if (result != 0) {
                    std::fprintf(stderr, "[recording] mirror transaction failed for stack %u: %d\n", layerStack, result);
                    return {};
                }
                SurfaceControl control{mirrorSurface.get()};
                control.mirrorOwnership = std::move(ownership);
                return control;
            }

            bool ZoomSurface(SurfaceControl &surface, float scaleX, float scaleY, uint32_t orientation, bool offset = false) {
                if (nullptr == surface.data)
                    return false;

                static SurfaceComposerClientTransaction transaction;
                StrongPointer<void> surfacePtr{surface.data};
                
                // Use SetMatrix to apply scaling transformation
                // SetMatrix parameters: dsdx, dtdx, dtdy, dsdy
                // For scaling: dsdx=scaleX, dtdx=0, dtdy=0, dsdy=scaleY
                if (14 <= Functionals::GetInstance().systemVersion && offset) {
                    float dsdx, dtdx, dtdy, dsdy;
                    switch (orientation) {
                        case 0:
                            dsdx = scaleX;
                            dtdx = 0.0f;
                            dtdy = 0.0f;
                            dsdy = scaleY;
                            break;
                        case 1:
                            dsdx = 0.0f;
                            dtdx = scaleY;
                            dtdy = -scaleX;
                            dsdy = 0.0f;
                            break;
                        case 2:
                            dsdx = -scaleX;
                            dtdx = 0.0f;
                            dtdy = 0.0f;
                            dsdy = -scaleY;
                            break;
                        case 3:
                            dsdx = 0.0f;
                            dtdx = -scaleY;
                            dtdy = scaleX;
                            dsdy = 0.0f;
                            break;
                        default:
                            return false;
                    }
                    transaction.SetMatrix(surfacePtr, dsdx, dtdx, dtdy, dsdy);
                    SURFACE_LOG_DEBUG("ZoomSurface called with dsdx: %f, dtdx: %f, dtdy: %f, dsdy: %f", dsdx, dtdx, dtdy, dsdy);
                } else {
                    transaction.SetMatrix(surfacePtr, scaleX, 0, 0, scaleY);
                    SURFACE_LOG_DEBUG("ZoomSurface called with scaleX: %f, scaleY: %f", scaleX, scaleY);
                }
                return transaction.Apply(false, false) == 0;
            }
        };

    } // namespace detail

    class ANativeWindowCreator {
    public:
        struct DisplayInfo {
            int32_t orientation;
            int32_t width;
            int32_t height;
        };

    public:
        static detail::SurfaceComposerClient &GetComposerInstance() {
            static detail::SurfaceComposerClient surfaceComposerClient;
            return surfaceComposerClient;
        }

        static DisplayInfo GetDisplayInfo() {
            auto &surfaceComposerClient = GetComposerInstance();
            detail::ui::DisplayState displayInfo{};

            if (!surfaceComposerClient.GetDisplayInfo(&displayInfo))
                return {};
            
            DisplayInfo local_displayInfo{0};   
            int32_t local_orientation = static_cast<int32_t>(displayInfo.orientation);  
            int32_t local_abs_x = (displayInfo.layerStackSpaceRect.width > displayInfo.layerStackSpaceRect.height ? displayInfo.layerStackSpaceRect.width : displayInfo.layerStackSpaceRect.height);
            int32_t local_abs_y = (displayInfo.layerStackSpaceRect.width < displayInfo.layerStackSpaceRect.height ? displayInfo.layerStackSpaceRect.width : displayInfo.layerStackSpaceRect.height);          
            if (local_orientation == 1 || local_orientation == 3) {
                local_displayInfo.width = local_abs_x;
                local_displayInfo.height = local_abs_y;
            } else {
                local_displayInfo.width = local_abs_y;
                local_displayInfo.height = local_abs_x;
            }
            local_displayInfo.orientation = local_orientation;
            return local_displayInfo;
        }

        static ANativeWindow *Create(const char *name, int32_t width = -1, int32_t height = -1, bool skipScrenshot_ = false) {
            auto &surfaceComposerClient = GetComposerInstance();
            
            // Auto-retrieve display dimensions
            while (-1 == width || -1 == height) {
                detail::ui::DisplayState displayInfo{};
                if (!surfaceComposerClient.GetDisplayInfo(&displayInfo))
                    break;

                width = displayInfo.layerStackSpaceRect.width;
                height = displayInfo.layerStackSpaceRect.height;

                break;
            }

            // Create Surface
            auto surfaceControl = surfaceComposerClient.CreateSurface(name, width, height, 0, skipScrenshot_);
            if (!surfaceControl.data) {
                SURFACE_LOG_ERROR("Failed to create surface control for: %s", name);
                return nullptr;
            }

            auto nativeWindow = reinterpret_cast<ANativeWindow *>(surfaceControl.GetSurface());
            if (!nativeWindow) {
                SURFACE_LOG_ERROR("Failed to get native window from surface control");
                return nullptr;
            }

            // Cache Surface controller
            m_cachedSurfaceControl.emplace(nativeWindow, std::move(surfaceControl));
            
            SURFACE_LOG_INFO("ANativeWindow created successfully: %p", nativeWindow);
            return nativeWindow;
        }

        static void Destroy(ANativeWindow *nativeWindow) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end())
                return;

            SURFACE_LOG_INFO("Destroying ANativeWindow: %p", nativeWindow);
            
            ClearAllMirrorSurfaces();

            // Destroy main Surface
            m_cachedSurfaceControl[nativeWindow].DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            m_cachedSurfaceControl.erase(nativeWindow);
            
            // If this is the last Surface, clear all mirror surfaces
            if (m_cachedSurfaceControl.empty()) {
                SURFACE_LOG_INFO("Last surface destroyed, clearing all mirror surfaces");
                ClearAllMirrorSurfaces();
            }
        }

        // Handle multi-display mirroring, this is the key feature for solving permission issues
        static void ProcessMirrorDisplay() {
            auto& lastTime = GetRecordingMirrors().lastPoll;

            if (13 > detail::Functionals::GetInstance().systemVersion)
                return;

            // Each pass below fork+execs `dumpsys display`, which binder-calls
            // DisplayManagerService and formats a large text dump — a costly
            // thing to do on a timer. It only exists to notice a newly-added
            // display (screen recorder, cast) so a mirror layer can be made
            // for it, so a few seconds of latency there is harmless.
            if (std::chrono::steady_clock::now() - lastTime < std::chrono::seconds(3))
                return;

            lastTime = std::chrono::steady_clock::now();
            // Run "dumpsys display" and get result
            auto pipe = popen("dumpsys display", "r");
            if (!pipe)
            {
                SURFACE_LOG_ERROR("Failed to run dumpsys command");
                return;
            }

            char buffer[512]{};
            std::string dumpDisplayResult;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
                dumpDisplayResult += buffer;
            const int result = pclose(pipe);
            if (result != 0) {
                std::fprintf(stderr, "[recording] dumpsys display failed: %d\n", result);
                return;
            }
            ProcessMirrorDisplayDump(dumpDisplayResult);
        }

        static void ProcessMirrorDisplayDump(std::string_view dumpDisplayResult) {
            if (13 > detail::Functionals::GetInstance().systemVersion) return;
            auto& cache = GetRecordingMirrors();

            auto& cachedLayerStackMirrorSurfaces = cache.surfaces;
            auto& cachedLayerStackIsOffset = cache.offsets;
            auto& cachedLayerStackScales = cache.scales;
            auto& cachedLayerStackPosition = cache.positions;

            const auto dumpDisplayInfos = detail::ParseDumpDisplayInfo(dumpDisplayResult);
            if (dumpDisplayInfos.empty()) {
                std::fprintf(stderr, "[recording] no complete active display records in dumpsys; retaining existing mirrors\n");
                return;
            }
            detail::ui::DisplayState primary{};
            uint32_t primaryStack = 0;
            const bool primaryKnown = GetComposerInstance().GetDisplayInfo(&primary);
            if (primaryKnown && primary.layerStack.id != UINT32_MAX)
                primaryStack = primary.layerStack.id;
            int32_t builtinDisplayWidth = -1, builtinDisplayHeight = -1, builtinDisplayOrientation = 0;
            for (const auto& display : dumpDisplayInfos) {
                if (display.currentLayerStack != primaryStack) continue;
                const auto& rect = display.currentLayerStackRect;
                builtinDisplayWidth = rect.right - rect.left;
                builtinDisplayHeight = rect.bottom - rect.top;
                builtinDisplayOrientation = display.orientation;
                if (display.orientation == 1 || display.orientation == 3)
                    std::swap(builtinDisplayWidth, builtinDisplayHeight);
                break;
            }
            if ((builtinDisplayWidth <= 0 || builtinDisplayHeight <= 0) && primaryKnown &&
                primary.layerStackSpaceRect.width > 0 && primary.layerStackSpaceRect.height > 0) {
                builtinDisplayWidth = std::min(primary.layerStackSpaceRect.width, primary.layerStackSpaceRect.height);
                builtinDisplayHeight = std::max(primary.layerStackSpaceRect.width, primary.layerStackSpaceRect.height);
                builtinDisplayOrientation = static_cast<int32_t>(primary.orientation);
                if (builtinDisplayOrientation < 0 || builtinDisplayOrientation > 3) return;
                std::fprintf(stderr, "[recording] primary dump record missing; using display-state geometry\n");
            }
            std::unordered_map<uint32_t, std::string> active;
            for (const auto& display : dumpDisplayInfos) {
                if (display.currentLayerStack != primaryStack && display.currentLayerStack != kGlassCaptureStack)
                    active.emplace(display.currentLayerStack, display.uniqueId);
            }
            if (cache.parsedCount != dumpDisplayInfos.size() || cache.targetCount != active.size()) {
                const auto& fn = detail::Functionals::GetInstance();
                std::fprintf(stderr, "[recording] parsed displays=%zu, external targets=%zu, mirrorSurface=%s\n",
                    dumpDisplayInfos.size(), active.size(), fn.SurfaceComposerClient__MirrorSurface ? "1-arg" :
                    fn.SurfaceComposerClient__MirrorSurface2 ? "2-arg" :
                    fn.SurfaceComposerClient__MirrorSurface3 ? "3-arg" : "unavailable");
                cache.parsedCount = dumpDisplayInfos.size(); cache.targetCount = active.size();
            }
            for (auto it = cache.surfaces.begin(); it != cache.surfaces.end();) {
                const auto device = active.find(it->first);
                if (device == active.end() || cache.identities[it->first] != device->second) {
                    const auto stack = it->first;
                    it = cache.surfaces.erase(it);
                    cache.offsets.erase(stack); cache.scales.erase(stack);
                    cache.positions.erase(stack); cache.geometry.erase(stack);
                    cache.identities.erase(stack);
                } else ++it;
            }
            if (builtinDisplayWidth <= 0 || builtinDisplayHeight <= 0) {
                std::fprintf(stderr, "[recording] no valid primary display record (parsed=%zu, primary_stack=%u)\n",
                             dumpDisplayInfos.size(), primaryStack);
                return;
            }
            for (const auto& displayInfo : dumpDisplayInfos) {
                if (displayInfo.currentLayerStack == primaryStack || displayInfo.currentLayerStack == kGlassCaptureStack)
                    continue;
                const auto stack = displayInfo.currentLayerStack;
                const auto& rect = displayInfo.currentLayerStackRect;
                const std::array<int32_t, 7> geometry{{builtinDisplayWidth, builtinDisplayHeight,
                    builtinDisplayOrientation, rect.left, rect.top, rect.right, rect.bottom}};
                const auto oldGeometry = cache.geometry.find(stack);
                if (oldGeometry == cache.geometry.end() || oldGeometry->second != geometry) {
                    cache.offsets.erase(stack); cache.scales.erase(stack); cache.positions.erase(stack);
                    cache.geometry[stack] = geometry;
                }
                int32_t surfaceDisplayWidth = -1, surfaceDisplayHeight = -1;
                surfaceDisplayWidth = std::min(rect.bottom - rect.top, rect.right - rect.left);
                surfaceDisplayHeight = std::max(rect.bottom - rect.top, rect.right - rect.left);

                bool offset = false;
                if (cachedLayerStackIsOffset.find(displayInfo.currentLayerStack) == cachedLayerStackIsOffset.end())
                {
                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3)
                    {
                        if (surfaceDisplayHeight == displayInfo.currentLayerStackRect.right)
                        {
                            cachedLayerStackIsOffset.emplace(displayInfo.currentLayerStack, false);
                        } else {
                            cachedLayerStackIsOffset.emplace(displayInfo.currentLayerStack, true);
                        }
                        offset = cachedLayerStackIsOffset[displayInfo.currentLayerStack];
                    }
                } else {
                    offset = cachedLayerStackIsOffset[displayInfo.currentLayerStack];
                }
                

                if (cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) == cachedLayerStackMirrorSurfaces.end())
                {
                    SURFACE_LOG_INFO("New display layerstack detected: [%s] -> %u", displayInfo.uniqueId.data(), displayInfo.currentLayerStack);

                    for (auto &[_, surfaceControl] : m_cachedSurfaceControl)
                    {
                        auto mirrorLayer = GetComposerInstance().MirrorSurface(surfaceControl, displayInfo.currentLayerStack);
                        if (mirrorLayer.data) {
                            SURFACE_LOG_INFO("Mirror layer created: %p", mirrorLayer.data);
                            cachedLayerStackMirrorSurfaces.emplace(displayInfo.currentLayerStack, std::move(mirrorLayer));
                            cache.identities[stack] = displayInfo.uniqueId;
                            std::fprintf(stderr, "[recording] mirrored window to stack %u (%s), source %dx%d, target %dx%d\n",
                                         stack, displayInfo.uniqueId.c_str(), builtinDisplayWidth, builtinDisplayHeight,
                                         surfaceDisplayWidth, surfaceDisplayHeight);
                            break; // Only create one mirror per layerStack
                        }
                    }
                }

                // Handle scaling for different display sizes
                if (-1 != builtinDisplayWidth && -1 != builtinDisplayHeight && cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) != cachedLayerStackMirrorSurfaces.end())
                {
                    if (cachedLayerStackScales.find(displayInfo.currentLayerStack) == cachedLayerStackScales.end())
                    {
                        auto &mirrorLayer = cachedLayerStackMirrorSurfaces.at(displayInfo.currentLayerStack);

                        float scaleX = static_cast<float>(surfaceDisplayWidth) / builtinDisplayWidth, scaleY = static_cast<float>(surfaceDisplayHeight) / builtinDisplayHeight;
                        if (scaleY < scaleX)
                        {
                            scaleX = scaleY;
                        } else {
                            scaleY = scaleX;
                        }
                        
                        if (GetComposerInstance().ZoomSurface(mirrorLayer, scaleX, scaleY, builtinDisplayOrientation, offset))
                            cachedLayerStackScales.emplace(displayInfo.currentLayerStack);
                        else std::fprintf(stderr, "[recording] mirror scale transaction failed for stack %u\n", stack);
                        SURFACE_LOG_INFO("Update mirror layer scale: %p %f %f", mirrorLayer.data, scaleX, scaleY);
                    }
                }
                // Apply transform to all cached surfaces if needed
                if (cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) != cachedLayerStackMirrorSurfaces.end()) {
                    auto &mirrorLayer = cachedLayerStackMirrorSurfaces.at(displayInfo.currentLayerStack);
                    if (mirrorLayer.data) {
                        // Apply position transform based on orientation
                        if (cachedLayerStackPosition.find(displayInfo.currentLayerStack) == cachedLayerStackPosition.end()) {
                            static detail::SurfaceComposerClientTransaction transaction;
                            detail::StrongPointer<void> surfacePtr{mirrorLayer.data};
                            float x = 0, y = 0;
                            float scaleX = static_cast<float>(surfaceDisplayWidth) / builtinDisplayWidth, scaleY = static_cast<float>(surfaceDisplayHeight) / builtinDisplayHeight;
                            int index = 0;
                            if (scaleX <= scaleY) {
                                scaleY = scaleX;
                                index = 1;
                            } else if (scaleY <= scaleX) {
                                scaleX = scaleY;
                                index = 2;
                            }
                            if (14 <= detail::Functionals::GetInstance().systemVersion && offset) {
                                switch (builtinDisplayOrientation) {
                                    case 0: // ROT_0
                                        if (index == 1) {
                                            y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                        } else if (index == 2) {
                                            x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                        }
                                        break;
                                    case 1: // ROT_90
                                        if (index == 1) {
                                            x = surfaceDisplayWidth - (surfaceDisplayWidth - builtinDisplayWidth * scaleY) / 2;
                                        } else if (index == 2) {
                                            x =  surfaceDisplayWidth;
                                            y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                        }
                                        break;
                                    case 2: // ROT_180
                                        if (index == 1) {
                                            x = surfaceDisplayWidth - (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                            y = surfaceDisplayHeight;
                                        } else if (index == 2) {
                                            x = surfaceDisplayWidth;
                                            y = surfaceDisplayHeight - (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                        }
                                        break;
                                    case 3: // ROT_270
                                        if (index == 1) {
                                            x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                            y = surfaceDisplayHeight;
                                        } else if (index == 2) {
                                            y = builtinDisplayHeight - (surfaceDisplayHeight - builtinDisplayHeight * scaleX) / 2;
                                        }
                                        break;
                                }
                            } else {
                                if (index == 1) {
                                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3) {
                                        x = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                    } else {
                                        y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                    }
                                } else if (index == 2) {
                                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3) {
                                        y = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                    } else {
                                        x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                    }
                                }
                            }
                            transaction.SetPosition(surfacePtr, x, y);
                            const int32_t result = transaction.Apply(false, false);
                            if (result == 0) cachedLayerStackPosition.emplace(displayInfo.currentLayerStack);
                            else std::fprintf(stderr, "[recording] mirror position transaction failed for stack %u: %d\n", stack, result);
                            SURFACE_LOG_INFO("Update mirror layer position: %d %f %p %f %f", index, scaleX, mirrorLayer.data, x, y);
                        }
                    }
                }
            }

        }

        // Enable automatic mirror display handling (recommended to call periodically in main loop)
        static void EnableAutoMirrorDisplay(bool enable = true) {
            static bool autoMirrorEnabled = false;
            SURFACE_LOG_INFO("EnableAutoMirrorDisplay called with enable=%s", enable ? "true" : "false");
            autoMirrorEnabled = enable;
            
            if (enable) {
                SURFACE_LOG_INFO("Auto mirror display enabled, calling ProcessMirrorDisplay immediately");
                ProcessMirrorDisplay(); // Execute immediately once
            } else {
                SURFACE_LOG_INFO("Auto mirror display disabled");
            }
        }

        // Get current cached Surface count
        static size_t GetCachedSurfaceCount() {
            return m_cachedSurfaceControl.size();
        }

        // Clear all mirror surfaces
        static void ClearAllMirrorSurfaces() {
            SURFACE_LOG_INFO("Clearing all mirror surfaces...");
            
            // Clear cached mirrors from ProcessMirrorDisplay
            ClearLayerStackMirrorSurfaces();
            
            SURFACE_LOG_INFO("All mirror surfaces cleared");
        }

        // Clear mirror surface for specific LayerStack
        static void ClearMirrorSurfaceForLayerStack(const std::string& layerStack) {
            SURFACE_LOG_INFO("Clearing mirror surface for layerStack: %s", layerStack.c_str());
            
            auto& cachedMirrors = GetLayerStackMirrorSurfaces();
            uint32_t stack = 0;
            if (!ParseStackKey(layerStack, &stack)) return;
            auto it = cachedMirrors.find(stack);
            if (it != cachedMirrors.end()) {
                // SurfaceControl destructor will automatically handle cleanup
                cachedMirrors.erase(it);
                auto& cache = GetRecordingMirrors();
                cache.offsets.erase(stack); cache.scales.erase(stack);
                cache.positions.erase(stack); cache.geometry.erase(stack);
                cache.identities.erase(stack);
                SURFACE_LOG_INFO("Mirror surface for layerStack %s cleared", layerStack.c_str());
            }
        }

        // Get current mirror surface count
        static size_t GetMirrorSurfaceCount() {
            return GetLayerStackMirrorSurfaces().size();
        }

        // Check if mirror exists for specific LayerStack
        static bool HasMirrorForLayerStack(const std::string& layerStack) {
            auto& cachedMirrors = GetLayerStackMirrorSurfaces();
            uint32_t stack = 0;
            return ParseStackKey(layerStack, &stack) && cachedMirrors.find(stack) != cachedMirrors.end();
        }

        // Hides a layer from screen captures — including our own mirror.
        //
        // Sampling the mirror to draw the window's glass puts the window's own
        // output back into the next mirrored frame, and since each pass adds
        // the UI on top of what it already showed, the loop saturates to white
        // within a few frames. Excluding ourselves is what breaks it.
        //
        // Set through a transaction rather than at creation so it can follow
        // the mirror being switched on and off without rebuilding the surface.
        static bool SetSkipScreenshot(ANativeWindow *nativeWindow, bool skip) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end()) return false;

            constexpr uint32_t kLayerSkipScreenshot = 0x40; // layer_state_t
            detail::SurfaceComposerClientTransaction t;
            detail::StrongPointer<void> sc{};
            sc.pointer = it->second.data;
            if (!t.SetFlags(sc, skip ? kLayerSkipScreenshot : 0u, kLayerSkipScreenshot))
                return false;
            return t.Apply(false, false) == 0;
        }

        // ── Virtual-display capture probe ───────────────────────────────
        //
        // Whether the symbols needed to have SurfaceFlinger composite the
        // screen into a Surface we own all resolved. That is the only route
        // to live screen pixels we can sample (for refraction-style glass),
        // and unlike the blur API it is old enough to exist on early Android
        // — but only if this ROM's libgui exports it under the manglings we
        // probe for, which mirrorSurface already proved is not a given.
        //
        // `missing` receives a comma-separated list of whichever failed, so a
        // device that can't do it says which piece is absent instead of just
        // silently doing nothing.
        // First physical display id, for mirrorDisplay().
        static bool GetPrimaryPhysicalDisplayId(detail::ui::PhysicalDisplayId* out) {
            auto fn = detail::Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayIds;
            if (nullptr == fn) return false;
            auto ids = fn();
            if (ids.empty()) return false;
            *out = ids[0];
            return true;
        }

        static bool ScreenCaptureSupported(std::string* missing = nullptr) {
            const auto& f = detail::Functionals::GetInstance();
            struct { const char* name; bool available; } syms[] = {
                {"createDisplay", f.SurfaceComposerClient__CreateVirtualDisplay_withOwnerUid ||
                                  f.SurfaceComposerClient__CreateVirtualDisplay ||
                                  f.SurfaceComposerClient__CreateVirtualDisplay_withoutPower ||
                                  (f.SurfaceComposerClient__CreateDisplay && f.String8__Constructor && f.String8__Destructor)},
                {"destroyDisplay", f.SurfaceComposerClient__DestroyVirtualDisplay || f.SurfaceComposerClient__DestroyDisplay},
                {"setDisplaySurface(IGraphicBufferProducer)", f.SurfaceComposerClient__Transaction__SetDisplaySurface != nullptr},
                {"setDisplayLayerStack", f.SurfaceComposerClient__Transaction__SetDisplayLayerStack || f.SurfaceComposerClient__Transaction__SetDisplayLayerStack_legacy},
                {"setDisplayProjection", f.SurfaceComposerClient__Transaction__SetDisplayProjection || f.SurfaceComposerClient__Transaction__SetDisplayProjection_legacy},
                {"getIGraphicBufferProducer", f.Surface__GetIGraphicBufferProducer != nullptr},
                {"Transaction constructor", f.SurfaceComposerClient__Transaction__Constructor != nullptr},
                {"Transaction::apply", f.SurfaceComposerClient__Transaction__Apply != nullptr},
            };
            bool ok = true;
            for (const auto& s : syms) {
                if (s.available) continue;
                ok = false;
                if (missing) {
                    if (!missing->empty()) *missing += ", ";
                    *missing += s.name;
                }
            }
            return ok;
        }

        // Complete cleanup when application exits
        static void Cleanup() {
            SURFACE_LOG_INFO("Performing complete cleanup...");
            
            // Clean up all main surfaces
            for (auto& [nativeWindow, surfaceControl] : m_cachedSurfaceControl) {
                SURFACE_LOG_DEBUG("Cleaning up surface: %p", nativeWindow);
                surfaceControl.DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            }
            m_cachedSurfaceControl.clear();
            
            // Clear all mirror surfaces
            ClearAllMirrorSurfaces();
            
            SURFACE_LOG_INFO("Complete cleanup finished");
        }

    private:
        inline static std::unordered_map<ANativeWindow *, detail::SurfaceControl> m_cachedSurfaceControl;

        static constexpr uint32_t kGlassCaptureStack = 0x41493344;
        struct RecordingMirrors {
            std::unordered_map<uint32_t, detail::SurfaceControl> surfaces;
            std::unordered_map<uint32_t, bool> offsets;
            std::unordered_set<uint32_t> scales, positions;
            std::unordered_map<uint32_t, std::array<int32_t, 7>> geometry;
            std::unordered_map<uint32_t, std::string> identities;
            std::chrono::steady_clock::time_point lastPoll{};
            size_t parsedCount = SIZE_MAX, targetCount = SIZE_MAX;
        };
        static RecordingMirrors& GetRecordingMirrors() {
            static RecordingMirrors cache;
            return cache;
        }
        static std::unordered_map<uint32_t, detail::SurfaceControl>& GetLayerStackMirrorSurfaces() {
            return GetRecordingMirrors().surfaces;
        }
        static bool ParseStackKey(const std::string& text, uint32_t* result) {
            if (text.empty()) return false;
            uint64_t value = 0;
            for (char ch : text) {
                if (ch < '0' || ch > '9') return false;
                value = value * 10 + static_cast<unsigned>(ch - '0');
                if (value > UINT32_MAX) return false;
            }
            *result = static_cast<uint32_t>(value);
            return true;
        }
        static void ClearLayerStackMirrorSurfaces() {
            auto& cache = GetRecordingMirrors();
            cache.surfaces.clear(); // Releases each clone and its root.
            cache.offsets.clear(); cache.scales.clear(); cache.positions.clear();
            cache.geometry.clear(); cache.identities.clear(); cache.lastPoll = {};
            cache.parsedCount = cache.targetCount = SIZE_MAX;
        }
    };
}

#undef SURFACE_STARTUP_TRACE
#undef ResolveMethod

#endif // !A_NATIVE_WINDOW_CREATOR_H
