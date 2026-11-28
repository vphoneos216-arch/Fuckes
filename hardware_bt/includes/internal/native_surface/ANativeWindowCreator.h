#include "release_log.hpp"
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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <climits>

// Log system configuration
#ifndef SURFACE_LOG_TAG
#define SURFACE_LOG_TAG "AImGui"
#endif

#ifndef SURFACE_LOG_ENABLE
#define SURFACE_LOG_ENABLE TOUCHBASE_DIAGNOSTICS
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
    #define SURFACE_LOG_ERROR(fmt, args...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_ERROR) \
                __android_log_print(ANDROID_LOG_ERROR, SURFACE_LOG_TAG, "[-] " fmt , ##args); \
        } while(0)
    
    #define SURFACE_LOG_WARN(fmt, args...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_WARN) \
                __android_log_print(ANDROID_LOG_WARN, SURFACE_LOG_TAG, "[!] " fmt , ##args); \
        } while(0)
    
    #define SURFACE_LOG_INFO(fmt, args...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_INFO) \
                __android_log_print(ANDROID_LOG_INFO, SURFACE_LOG_TAG, "[+] " fmt , ##args); \
        } while(0)
    
    #define SURFACE_LOG_DEBUG(fmt, args...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                __android_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[*] " fmt , ##args); \
        } while(0)
    
    #define SURFACE_LOG_TRACE(fmt, args...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                __android_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[=] " fmt , ##args); \
        } while(0)
#else
    #define SURFACE_LOG_ERROR(fmt, args...)   ((void)0)
    #define SURFACE_LOG_WARN(fmt, args...)    ((void)0)
    #define SURFACE_LOG_INFO(fmt, args...)    ((void)0)
    #define SURFACE_LOG_DEBUG(fmt, args...)   ((void)0)
    #define SURFACE_LOG_TRACE(fmt, args...)   ((void)0)
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
            struct LayerStack {
                uint32_t id = UINT32_MAX;
            };

            enum class Rotation {
                Rotation0 = 0,
                Rotation90 = 1,
                Rotation180 = 2,
                Rotation270 = 3
            };

            struct Size {
                int32_t width = -1;
                int32_t height = -1;
            };

            struct DisplayState {
                LayerStack layerStack;
                Rotation orientation = Rotation::Rotation0;
                Size layerStackSpaceRect;
            };

            typedef int64_t nsecs_t; // nano-seconds
            struct DisplayInfo {
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

            enum class DisplayType {
                DisplayIdMain = 0,
                DisplayIdHdmi = 1
            };

            struct PhysicalDisplayId {
                uint64_t value;
            };

            struct Rect {
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
        struct StrongPointer {
            union {
                any_t *pointer;
                char padding[sizeof(std::max_align_t)];
            };
            inline any_t *operator->() const { return pointer; }
            inline any_t *get() const { return pointer; }
            inline explicit operator bool() const { return nullptr != pointer; }
        };

        struct Functionals {
            struct SymbolMethod {
                void *(*Open)(const char *filename, int flag) = nullptr;
                void *(*Find)(void *handle, const char *symbol) = nullptr;
                int (*Close)(void *handle) = nullptr;
            };

            size_t systemVersion = 13;

            void (*RefBase__IncStrong)(void *thiz, void *id) = nullptr;
            void (*RefBase__DecStrong)(void *thiz, void *id) = nullptr;

            void (*String8__Constructor)(void *thiz, const char *const data) = nullptr;
            void (*String8__Destructor)(void *thiz) = nullptr;

            void (*LayerMetadata__Constructor)(void *thiz) = nullptr;
            void (*LayerMetadata__setInt32)(void *thiz, uint32_t key, int32_t value) = nullptr;

            void (*SurfaceComposerClient__Constructor)(void *thiz) = nullptr;
            void (*SurfaceComposerClient__Destructor)(void *thiz) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, void *layerMetadata, uint32_t *outTransformHint) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and8)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, uint32_t windowType, uint32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and9)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, int32_t windowType, int32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface)(void *thiz, void *mirrorFromSurface) = nullptr;
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
            void (*Surface__DisConnect)(void *thiz, int32_t api) = nullptr;

            Functionals(const SymbolMethod &symbolMethod) {
                std::string systemVersionString(128, 0);
                systemVersionString.resize(__system_property_get("ro.build.version.release", systemVersionString.data()));
                if (!systemVersionString.empty())
                    systemVersion = std::stoi(systemVersionString);

                if (5 > systemVersion) {
                    SURFACE_LOG_ERROR("Unsupported system version: %zu", systemVersion);
                    return;
                }

                // Advanced patches table incorporating A17 RK signature override
                static std::unordered_map<size_t, std::unordered_map<void **, const char *>> patchesTable = {
                    { 17, {
                        {reinterpret_cast<void **>(&LayerMetadata__Constructor), "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetLayer), "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetTrustedOverlay), "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__Apply), "_ZN7android21SurfaceComposerClient11Transaction5applyEbb"},
                        {reinterpret_cast<void **>(&SurfaceControl__GetSurface), "_ZN7android14SurfaceControl10getSurfaceEv"},
                        {reinterpret_cast<void **>(&SurfaceControl__DisConnect), "_ZN7android14SurfaceControl10disconnectEv"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__GetPhysicalDisplayIds), "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__GetPhysicalDisplayToken), "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__GetDisplayState), "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE"},
                    }},
                    { 16, {
                        {reinterpret_cast<void **>(&LayerMetadata__Constructor), "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                    }},
                    { 15, {
                        {reinterpret_cast<void **>(&LayerMetadata__Constructor), "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                    }},
                    { 14, {
                        {reinterpret_cast<void **>(&LayerMetadata__Constructor), "_ZN7android3gui13LayerMetadataC2Ev"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                    }},
                    { 12, {
                        {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__Apply), "_ZN7android21SurfaceComposerClient11Transaction5applyEb"},
                    }},
                    { 11, {
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj"},
                        {reinterpret_cast<void **>(&SurfaceControl__GetSurface), "_ZNK7android14SurfaceControl10getSurfaceEv"},
                    }},
                    { 10, {
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE"},
                        {reinterpret_cast<void **>(&SurfaceControl__GetSurface), "_ZNK7android14SurfaceControl10getSurfaceEv"},
                    }},
                    { 9, {
                        {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface_and9), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii"},
                        {reinterpret_cast<void **>(&SurfaceComposerClient__GetBuiltInDisplay), "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi"},
                        {reinterpret_cast<void **>(&SurfaceControl__GetSurface), "_ZNK7android14SurfaceControl10getSurfaceEv"},
                    }},
                };

#ifdef __LP64__
                auto libgui = symbolMethod.Open("/system/lib64/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib64/libutils.so", RTLD_LAZY);
#else
                auto libgui = symbolMethod.Open("/system/lib/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib/libutils.so", RTLD_LAZY);
#endif

                // Resolve common baseline pointers needed for advanced mirroring & basic operations
                ResolveMethod(RefBase, IncStrong, libutils, "_ZNK7android7RefBase9incStrongEPKv");
                ResolveMethod(RefBase, DecStrong, libutils, "_ZNK7android7RefBase9decStrongEPKv");
                ResolveMethod(String8, Constructor, libutils, "_ZN7android7String8C2EPKc");
                ResolveMethod(String8, Destructor, libutils, "_ZN7android7String8D2Ev");

                ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android13LayerMetadataC2Ev"); 
                ResolveMethod(LayerMetadata, setInt32, libgui, "_ZN7android13LayerMetadata8setInt32Eji"); 
                ResolveMethod(SurfaceComposerClient, Constructor, libgui, "_ZN7android21SurfaceComposerClientC2Ev");
                ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj");
                
                ResolveMethod(SurfaceComposerClient, MirrorSurface, libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlE"); 
                ResolveMethod(SurfaceComposerClient, GetInternalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv"); 
                ResolveMethod(SurfaceComposerClient, GetDisplayState, libgui, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
                ResolveMethod(SurfaceComposerClient, GetDisplayInfo, libgui, "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE"); 
                ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayIds, libgui, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv"); 
                ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE"); 
                ResolveMethod(SurfaceComposerClient, OpenGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv");
                ResolveMethod(SurfaceComposerClient, CloseGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb");

                ResolveMethod(SurfaceComposerClient__Transaction, Constructor, libgui, "_ZN7android21SurfaceComposerClient11TransactionC2Ev"); 
                ResolveMethod(SurfaceComposerClient__Transaction, SetLayer, libgui, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
                ResolveMethod(SurfaceComposerClient__Transaction, SetTrustedOverlay, libgui, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb"); 
                ResolveMethod(SurfaceComposerClient__Transaction, SetLayerStack, libgui, "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE"); 
                ResolveMethod(SurfaceComposerClient__Transaction, Show, libgui, "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE"); 
                ResolveMethod(SurfaceComposerClient__Transaction, Hide, libgui, "_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE"); 
                ResolveMethod(SurfaceComposerClient__Transaction, Reparent, libgui, "_ZN7android21SurfaceComposerClient11Transaction8reparentERKNS_2spINS_14SurfaceControlEEES6_"); 
                ResolveMethod(SurfaceComposerClient__Transaction, SetMatrix, libgui, "_ZN7android21SurfaceComposerClient11Transaction9setMatrixERKNS_2spINS_14SurfaceControlEEEffff"); 
                ResolveMethod(SurfaceComposerClient__Transaction, SetPosition, libgui, "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff"); 
                ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb"); 

                ResolveMethod(SurfaceControl, Validate, libgui, "_ZNK7android14SurfaceControl8validateEv");
                ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZN7android14SurfaceControl10getSurfaceEv"); 
                ResolveMethod(SurfaceControl, DisConnect, libgui, "_ZN7android14SurfaceControl10disconnectEv"); 

                // Apply targeted patches
                auto it = patchesTable.find(systemVersion);
                if (it != patchesTable.end()) {
                    for (const auto &[patchTo, signature] : patchesTable.at(systemVersion)) {
                        *patchTo = symbolMethod.Find(libgui, signature);
                        if (nullptr == *patchTo) SURFACE_LOG_ERROR("Patch method not found: %s", signature);
                    }
                }

                symbolMethod.Close(libutils);
                symbolMethod.Close(libgui);
            }

            static const Functionals &GetInstance(const SymbolMethod &symbolMethod = {.Open = dlopen, .Find = dlsym, .Close = dlclose}) {
                static Functionals functionals(symbolMethod);
                return functionals;
            }
        };

        struct String8 {
            char data[1024];
            String8(const char *const string) { Functionals::GetInstance().String8__Constructor(data, string); }
            ~String8() { Functionals::GetInstance().String8__Destructor(data); }
            operator void *() { return reinterpret_cast<void *>(data); }
        };

        struct LayerMetadata {
            char data[1024];
            LayerMetadata() {
                if (9 < Functionals::GetInstance().systemVersion) Functionals::GetInstance().LayerMetadata__Constructor(data);
            }
            void setInt32(uint32_t key, int32_t value) {
                if (Functionals::GetInstance().LayerMetadata__setInt32) Functionals::GetInstance().LayerMetadata__setInt32(data, key, value);
            }
            operator void *() {
                if (9 < Functionals::GetInstance().systemVersion) return reinterpret_cast<void *>(data);
                else return nullptr;
            }
        };

        struct Surface {};

        struct SurfaceControl {
            void *data;
            SurfaceControl() : data(nullptr) {}
            SurfaceControl(void *data) : data(data) {}

            int32_t Validate() {
                if (nullptr == data) return 0;
                return Functionals::GetInstance().SurfaceControl__Validate(data);
            }

            Surface *GetSurface() {
                if (nullptr == data) return nullptr;
                auto result = Functionals::GetInstance().SurfaceControl__GetSurface(data);
                return reinterpret_cast<Surface *>(reinterpret_cast<size_t>(result.pointer) + sizeof(std::max_align_t) / 2);
            }

            void DisConnect() {
                if (nullptr == data || !Functionals::GetInstance().SurfaceControl__DisConnect) return;
                Functionals::GetInstance().SurfaceControl__DisConnect(data);
            }

            void SetLayer(int32_t z) {
                if (nullptr == data || !Functionals::GetInstance().SurfaceControl__SetLayer) return;
                Functionals::GetInstance().SurfaceControl__SetLayer(data, z);
            }

            void DestroySurface(Surface *surface) {
                if (nullptr == data || nullptr == surface) return;
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

            SurfaceComposerClient() {
                Functionals::GetInstance().SurfaceComposerClient__Constructor(data);
                Functionals::GetInstance().RefBase__IncStrong(data, this);
            }

            SurfaceControl CreateSurface(const char *name, int32_t width, int32_t height, uint32_t windowFlags = 0, bool skipScrenshot = false) {
                void *parentHandle = nullptr;
                String8 windowName(name);
                int32_t pixelFormat = 1; // RGBA_8888
                LayerMetadata layerMetadata{};

                if (skipScrenshot && (Functionals::GetInstance().systemVersion == 10 || Functionals::GetInstance().systemVersion == 11)) {
                    layerMetadata.setInt32(2u, 441731);
                }
                
                if (skipScrenshot && Functionals::GetInstance().systemVersion >= 12) {
                    windowFlags |= 0x40; // eSkipScreenshot
                }

                // ABI fix to safely handle the parentHandle pointer correctly per OS version
                if (12 <= Functionals::GetInstance().systemVersion) {
                    static void *fakeParentHandleForBinder = nullptr;
                    parentHandle = &fakeParentHandleForBinder;
                }

                StrongPointer<void> result{};
                
                if (Functionals::GetInstance().systemVersion == 9) {
                    int32_t windowType = -1;
                    int32_t ownerUid = -1;
                    if (skipScrenshot) windowType = 441731;
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface_and9(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, windowType, ownerUid);
                } else if (Functionals::GetInstance().systemVersion >= 10) {
                    if (!Functionals::GetInstance().SurfaceComposerClient__CreateSurface) {
                        SURFACE_LOG_ERROR("CRITICAL: CreateSurface symbol is null!");
                        return {};
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                } else {
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                }

                if (nullptr == result.get()) {
                    SURFACE_LOG_ERROR("Failed to create surface: %s", name);
                    return {};
                }

                if (12 <= Functionals::GetInstance().systemVersion) {
                    static SurfaceComposerClientTransaction transaction;
                    transaction.SetTrustedOverlay(result, true);
                    transaction.SetLayer(result, INT_MAX);
                    transaction.Apply(false, true);
                } else if (8 >= Functionals::GetInstance().systemVersion) {
                    OpenGlobalTransaction();
                    SurfaceControl{result.get()}.SetLayer(INT_MAX);
                    CloseGlobalTransaction(false);
                }

                return {result.get()};
            }

            bool GetDisplayInfo(ui::DisplayState *displayInfo) {
                static StrongPointer<void> defaultDisplay;

                if (nullptr == defaultDisplay.get()) {
                    if (9 >= Functionals::GetInstance().systemVersion) { 
                        defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetBuiltInDisplay(ui::DisplayType::DisplayIdMain);
                    } else {
                        if (14 > Functionals::GetInstance().systemVersion) { 
                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetInternalDisplayToken();
                        } else { 
                            auto displayIds = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayIds();
                            if (displayIds.empty()) return false;
                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayToken(displayIds[0]);
                        }
                    }
                }

                if (nullptr == defaultDisplay.get())
                    return false;

                if (11 <= Functionals::GetInstance().systemVersion) { 
                    return 0 == Functionals::GetInstance().SurfaceComposerClient__GetDisplayState(defaultDisplay, displayInfo);
                } else { 
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

            SurfaceControl MirrorSurface(SurfaceControl &surface, uint32_t layerStack) {
                using mirror_surfaces_t = std::pair<void *, void *>;
                constexpr auto MirrorSurfacesDeleter = [](mirror_surfaces_t *pair) {
                    SurfaceControl fakeSurface;
                    if (pair->first) {
                        Functionals::GetInstance().SurfaceControl__DisConnect(pair->first);
                        Functionals::GetInstance().RefBase__DecStrong(pair->first, fakeSurface.data);
                    }
                    if (pair->second) {
                        Functionals::GetInstance().SurfaceControl__DisConnect(pair->second);
                        Functionals::GetInstance().RefBase__DecStrong(pair->second, fakeSurface.data);
                    }
                    delete pair;
                };

                using mirror_surfaces_proxy_t = std::unique_ptr<mirror_surfaces_t, decltype(MirrorSurfacesDeleter)>;

                if (13 > Functionals::GetInstance().systemVersion) return {};

                auto mirrorSurface = Functionals::GetInstance().SurfaceComposerClient__MirrorSurface(data, surface.data);
                if (nullptr == mirrorSurface.get()) return {};

                int32_t width = -1, height = -1;
                while (-1 == width || -1 == height) {
                    ui::DisplayState displayInfo{};
                    if (!GetDisplayInfo(&displayInfo)) break;
                    width = displayInfo.layerStackSpaceRect.width;
                    height = displayInfo.layerStackSpaceRect.height;
                    break;
                }

                auto mirrorRootName = "MirrorRoot@" + std::to_string(layerStack);
                auto mirrorRootSurface = CreateSurface(mirrorRootName.c_str(), width, height, 0x00004000);
                if (!mirrorRootSurface.data) return {};

                static SurfaceComposerClientTransaction transaction;
                static std::vector<mirror_surfaces_proxy_t> mirrorSurfaces;
                StrongPointer<void> mirrorRootPtr{mirrorRootSurface.data};
                StrongPointer<void> mirrorPtr{mirrorSurface.get()};
                
                transaction.SetLayer(mirrorRootPtr, INT_MAX);
                transaction.SetLayerStack(mirrorRootPtr, layerStack);
                transaction.Apply(false, true);

                transaction.SetLayerStack(mirrorPtr, layerStack);
                transaction.Show(mirrorPtr);
                transaction.Reparent(mirrorPtr, mirrorRootPtr);
                transaction.Apply(false, true);

                mirrorSurfaces.emplace_back(new mirror_surfaces_t{mirrorSurface.get(), mirrorRootSurface.data}, MirrorSurfacesDeleter);
                return {mirrorSurface.get()};
            }

            void ZoomSurface(SurfaceControl &surface, float scaleX, float scaleY, uint32_t orientation, bool offset = false) {
                if (nullptr == surface.data) return;
                static SurfaceComposerClientTransaction transaction;
                StrongPointer<void> surfacePtr{surface.data};
                
                if (14 <= Functionals::GetInstance().systemVersion && offset) {
                    float dsdx, dtdx, dtdy, dsdy;
                    switch (orientation) {
                        case 0: dsdx = scaleX; dtdx = 0.0f; dtdy = 0.0f; dsdy = scaleY; break;
                        case 1: dsdx = 0.0f; dtdx = scaleY; dtdy = -scaleX; dsdy = 0.0f; break;
                        case 2: dsdx = -scaleX; dtdx = 0.0f; dtdy = 0.0f; dsdy = -scaleY; break;
                        case 3: dsdx = 0.0f; dtdx = -scaleY; dtdy = scaleX; dsdy = 0.0f; break;
                    }
                    transaction.SetMatrix(surfacePtr, dsdx, dtdx, dtdy, dsdy);
                } else {
                    transaction.SetMatrix(surfacePtr, scaleX, 0, 0, scaleY);
                }
                transaction.Apply(false, true);
            }
        };

        struct DumpDisplayInfo {
            std::string uniqueId;
            uint32_t currentLayerStack;
            int32_t orientation = 0;
            std::string type;
            struct {
                int32_t left;
                int32_t top;
                int32_t right;
                int32_t bottom;
            } currentLayerStackRect;

            static DumpDisplayInfo MakeFromRawDumpInfo(const std::string_view &uniqueId, const std::string_view &currentLayerStack, const std::string_view &currentLayerStackRect, const std::string_view &orientation = "", const std::string_view &type = "") {
                DumpDisplayInfo result;
                result.uniqueId = std::string{uniqueId.begin(), uniqueId.end()};
                result.currentLayerStack = static_cast<uint32_t>(std::stoul(std::string{currentLayerStack.begin(), currentLayerStack.end()}));
                result.orientation = orientation.empty() ? 0 : std::stoi(std::string{orientation.begin(), orientation.end()});
                result.type = std::string{type.begin(), type.end()};

                auto leftPos = currentLayerStackRect.find("(") + 1;
                auto topPos = currentLayerStackRect.find(", ", leftPos);
                auto rightPos = currentLayerStackRect.find(" - ", topPos + 2);
                auto bottomPos = currentLayerStackRect.find(", ", rightPos + 3);
                auto endPos = currentLayerStackRect.find(")", bottomPos + 2);

                result.currentLayerStackRect.left = std::stoi(std::string{currentLayerStackRect.begin() + leftPos, currentLayerStackRect.begin() + topPos});
                result.currentLayerStackRect.top = std::stoi(std::string{currentLayerStackRect.begin() + topPos + 2, currentLayerStackRect.begin() + rightPos});
                result.currentLayerStackRect.right = std::stoi(std::string{currentLayerStackRect.begin() + rightPos + 3, currentLayerStackRect.begin() + bottomPos});
                result.currentLayerStackRect.bottom = std::stoi(std::string{currentLayerStackRect.begin() + bottomPos + 2, currentLayerStackRect.begin() + endPos});

                return result;
            }
        };

        inline std::vector<DumpDisplayInfo> ParseDumpDisplayInfo(const std::string_view &dumpDisplayInfo) {
            constexpr auto SubStringView = [](const std::string_view &str, std::string_view start, std::string_view end, int startOffset = 0) -> std::string_view {
                auto startIt = str.find(start, startOffset);
                if (std::string::npos == startIt) return {};
                auto endIt = str.find(end, startIt + start.size());
                if (std::string::npos == endIt) return {};
                return str.substr(startIt + start.size(), endIt - startIt - start.size());
            };

            std::vector<DumpDisplayInfo> result;
            auto dumpDisplayInfoIt = std::string_view::npos;
            
            while (std::string_view::npos != (dumpDisplayInfoIt = dumpDisplayInfo.find("DisplayDeviceInfo", dumpDisplayInfoIt + 1))) {
                auto type = SubStringView(dumpDisplayInfo, "type ", ",", dumpDisplayInfoIt);
                auto uniqueId = SubStringView(dumpDisplayInfo, "uniqueId=\"", "\"", dumpDisplayInfoIt);
                auto currentLayerStack = SubStringView(dumpDisplayInfo, "mCurrentLayerStack=", "\n", dumpDisplayInfoIt);
                auto currentLayerStackRect = SubStringView(dumpDisplayInfo, "mCurrentLayerStackRect=", "\n", dumpDisplayInfoIt);
                auto orientation = SubStringView(dumpDisplayInfo, "mCurrentOrientation=", "\n", dumpDisplayInfoIt);

                if ("-1" == currentLayerStack) continue;
                if (uniqueId.empty() || currentLayerStack.empty()) continue;

                result.push_back(DumpDisplayInfo::MakeFromRawDumpInfo(uniqueId, currentLayerStack, currentLayerStackRect, orientation, type));
            }
            return result;
        }
    }

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
            
            while (-1 == width || -1 == height) {
                detail::ui::DisplayState displayInfo{};
                if (!surfaceComposerClient.GetDisplayInfo(&displayInfo)) break;
                width = displayInfo.layerStackSpaceRect.width;
                height = displayInfo.layerStackSpaceRect.height;
                break;
            }

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

            m_cachedSurfaceControl.emplace(nativeWindow, std::move(surfaceControl));
            return nativeWindow;
        }

        static void Destroy(ANativeWindow *nativeWindow) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end()) return;

            m_cachedSurfaceControl[nativeWindow].DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            m_cachedSurfaceControl.erase(nativeWindow);
            
            if (m_cachedSurfaceControl.empty()) {
                ClearAllMirrorSurfaces();
            }
        }

        static void ProcessMirrorDisplay() {
            static std::chrono::steady_clock::time_point lastTime{};
            if (13 > detail::Functionals::GetInstance().systemVersion) return;
            if (std::chrono::steady_clock::now() - lastTime < std::chrono::seconds(1)) return;

            auto pipe = popen("dumpsys display", "r");
            if (!pipe) return;

            char buffer[512]{};
            std::string dumpDisplayResult;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) dumpDisplayResult += buffer;
            pclose(pipe);

            static std::unordered_map<uint32_t, detail::SurfaceControl> cachedLayerStackMirrorSurfaces;
            static std::unordered_map<uint32_t, bool> cachedLayerStackIsOffset;
            static std::unordered_set<uint32_t> cachedLayerStackScales;
            static std::unordered_set<uint32_t> cachedLayerStackPosition;

            auto dumpDisplayInfos = detail::ParseDumpDisplayInfo(dumpDisplayResult);
            for (auto &displayInfo : dumpDisplayInfos) {
                static int32_t builtinDisplayWidth = -1, builtinDisplayHeight = -1, builtinDisplayOrientation = 0;
                if (0 == displayInfo.currentLayerStack) {
                    builtinDisplayOrientation = displayInfo.orientation;
                    if (displayInfo.orientation == 1 || displayInfo.orientation == 3) {
                        builtinDisplayWidth = displayInfo.currentLayerStackRect.bottom;
                        builtinDisplayHeight = displayInfo.currentLayerStackRect.right;
                    } else {
                        builtinDisplayWidth = displayInfo.currentLayerStackRect.right;
                        builtinDisplayHeight = displayInfo.currentLayerStackRect.bottom;
                    }
                }

                if (0 == displayInfo.currentLayerStack) continue;

                int32_t surfaceDisplayWidth = -1, surfaceDisplayHeight = -1;
                surfaceDisplayWidth = displayInfo.currentLayerStackRect.bottom < displayInfo.currentLayerStackRect.right ? displayInfo.currentLayerStackRect.bottom : displayInfo.currentLayerStackRect.right;
                surfaceDisplayHeight = displayInfo.currentLayerStackRect.bottom > displayInfo.currentLayerStackRect.right ? displayInfo.currentLayerStackRect.bottom : displayInfo.currentLayerStackRect.right;

                bool offset = false;
                if (cachedLayerStackIsOffset.find(displayInfo.currentLayerStack) == cachedLayerStackIsOffset.end()) {
                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3) {
                        if (surfaceDisplayHeight == displayInfo.currentLayerStackRect.right) cachedLayerStackIsOffset.emplace(displayInfo.currentLayerStack, false);
                        else cachedLayerStackIsOffset.emplace(displayInfo.currentLayerStack, true);
                        offset = cachedLayerStackIsOffset[displayInfo.currentLayerStack];
                    }
                } else {
                    offset = cachedLayerStackIsOffset[displayInfo.currentLayerStack];
                }

                if (cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) == cachedLayerStackMirrorSurfaces.end()) {
                    for (auto &[_, surfaceControl] : m_cachedSurfaceControl) {
                        auto mirrorLayer = GetComposerInstance().MirrorSurface(surfaceControl, displayInfo.currentLayerStack);
                        if (mirrorLayer.data) {
                            cachedLayerStackMirrorSurfaces.emplace(displayInfo.currentLayerStack, std::move(mirrorLayer));
                            break; 
                        }
                    }
                }

                if (-1 != builtinDisplayWidth && -1 != builtinDisplayHeight && cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) != cachedLayerStackMirrorSurfaces.end()) {
                    static int32_t lastOrientation = -1;
                    if (cachedLayerStackScales.find(displayInfo.currentLayerStack) == cachedLayerStackScales.end() || lastOrientation != builtinDisplayOrientation) {
                        auto &mirrorLayer = cachedLayerStackMirrorSurfaces.at(displayInfo.currentLayerStack);
                        float scaleX = static_cast<float>(surfaceDisplayWidth) / builtinDisplayWidth, scaleY = static_cast<float>(surfaceDisplayHeight) / builtinDisplayHeight;
                        if (scaleY < scaleX) scaleX = scaleY; else scaleY = scaleX;
                        GetComposerInstance().ZoomSurface(mirrorLayer, scaleX, scaleY, builtinDisplayOrientation, offset);
                        cachedLayerStackScales.emplace(displayInfo.currentLayerStack);
                        lastOrientation = builtinDisplayOrientation;
                    }
                }
                
                if (cachedLayerStackMirrorSurfaces.find(displayInfo.currentLayerStack) != cachedLayerStackMirrorSurfaces.end()) {
                    auto &mirrorLayer = cachedLayerStackMirrorSurfaces.at(displayInfo.currentLayerStack);
                    if (mirrorLayer.data) {
                        static int32_t lastOrientation = -1;
                        if (builtinDisplayOrientation != lastOrientation || cachedLayerStackPosition.find(displayInfo.currentLayerStack) == cachedLayerStackPosition.end()) {
                            static detail::SurfaceComposerClientTransaction transaction;
                            detail::StrongPointer<void> surfacePtr{mirrorLayer.data};
                            float x = 0, y = 0;
                            float scaleX = static_cast<float>(surfaceDisplayWidth) / builtinDisplayWidth, scaleY = static_cast<float>(surfaceDisplayHeight) / builtinDisplayHeight;
                            int index = 0;
                            if (scaleX <= scaleY) { scaleY = scaleX; index = 1; } 
                            else if (scaleY <= scaleX) { scaleX = scaleY; index = 2; }
                            
                            if (14 <= detail::Functionals::GetInstance().systemVersion && offset) {
                                switch (builtinDisplayOrientation) {
                                    case 0:
                                        if (index == 1) y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                        else if (index == 2) x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                        break;
                                    case 1:
                                        if (index == 1) x = surfaceDisplayWidth - (surfaceDisplayWidth - builtinDisplayWidth * scaleY) / 2;
                                        else if (index == 2) { x = surfaceDisplayWidth; y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2; }
                                        break;
                                    case 2:
                                        if (index == 1) { x = surfaceDisplayWidth - (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2; y = surfaceDisplayHeight; } 
                                        else if (index == 2) { x = surfaceDisplayWidth; y = surfaceDisplayHeight - (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2; }
                                        break;
                                    case 3:
                                        if (index == 1) { x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2; y = surfaceDisplayHeight; } 
                                        else if (index == 2) y = builtinDisplayHeight - (surfaceDisplayHeight - builtinDisplayHeight * scaleX) / 2;
                                        break;
                                }
                            } else {
                                if (index == 1) {
                                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3) x = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                    else y = (surfaceDisplayHeight - builtinDisplayHeight * scaleY) / 2;
                                } else if (index == 2) {
                                    if (builtinDisplayOrientation == 1 || builtinDisplayOrientation == 3) y = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                    else x = (surfaceDisplayWidth - builtinDisplayWidth * scaleX) / 2;
                                }
                            }
                            transaction.SetPosition(surfacePtr, x, y);
                            transaction.Apply(false, true);
                            lastOrientation = builtinDisplayOrientation;
                            cachedLayerStackPosition.emplace(displayInfo.currentLayerStack);
                        }
                    }
                }
            }
            lastTime = std::chrono::steady_clock::now();
        }

        static void EnableAutoMirrorDisplay(bool enable = true) {
            static bool autoMirrorEnabled = false;
            autoMirrorEnabled = enable;
            if (enable) ProcessMirrorDisplay(); 
        }

        static size_t GetCachedSurfaceCount() { return m_cachedSurfaceControl.size(); }

        static void ClearAllMirrorSurfaces() { ClearLayerStackMirrorSurfaces(); }

        static void ClearMirrorSurfaceForLayerStack(const std::string& layerStack) {
            auto& cachedMirrors = GetLayerStackMirrorSurfaces();
            auto it = cachedMirrors.find(layerStack);
            if (it != cachedMirrors.end()) cachedMirrors.erase(it);
        }

        static size_t GetMirrorSurfaceCount() { return GetLayerStackMirrorSurfaces().size(); }

        static bool HasMirrorForLayerStack(const std::string& layerStack) {
            auto& cachedMirrors = GetLayerStackMirrorSurfaces();
            return cachedMirrors.find(layerStack) != cachedMirrors.end();
        }

        static void Cleanup() {
            for (auto& [nativeWindow, surfaceControl] : m_cachedSurfaceControl) {
                surfaceControl.DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            }
            m_cachedSurfaceControl.clear();
            ClearAllMirrorSurfaces();
        }

    private:
        inline static std::unordered_map<ANativeWindow *, detail::SurfaceControl> m_cachedSurfaceControl;

        static std::unordered_map<std::string, detail::SurfaceControl>& GetLayerStackMirrorSurfaces() {
            static std::unordered_map<std::string, detail::SurfaceControl> cachedLayerStackMirrorSurfaces;
            return cachedLayerStackMirrorSurfaces;
        }

        static void ClearLayerStackMirrorSurfaces() {
            auto& cachedMirrors = GetLayerStackMirrorSurfaces();
            cachedMirrors.clear();
        }
    };
}

#undef ResolveMethod

#endif // !A_NATIVE_WINDOW_CREATOR_H

