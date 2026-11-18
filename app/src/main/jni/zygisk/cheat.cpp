// [UPDATED] was <esp.h> (lowercase) — on a case-sensitive filesystem that
// resolves to others/imagens/esp.h (a PNG byte array, unrelated), not the
// real ESP feature header gsmodz/ESP.h. Confirmed from the actual CI log:
// "redefinition of 'esp_data'" (the PNG array got pulled in twice) plus a
// cascade of missing ImGui-internal/String/std::map symbols in layout.h
// that gsmodz/ESP.h's own include chain was supposed to provide.
#include <ESP.h>
#include <layout.h>
#include <imports.h>
#include <jni.h>
#include "DebugLog.h"
class _BYTE; class _QWORD;
class _DWORD; class _WORD;
#define _QWORD long 
#define _DWORD long
#define _BYTE long 
#define _WORD long
#include <EGL/egl.h>
//JavaVM *jvm;
//JNIEnv *genv;

EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
eglQuerySurface(dpy, surface, EGL_WIDTH, &egl.width);
eglQuerySurface(dpy, surface, EGL_HEIGHT, &egl.height);
if (!egl.setup) {
ImGui::CreateContext();
SetupImgui();InitTexture();
egl.setup = true;
}
ImGuiIO &io = ImGui::GetIO();ImGui_ImplOpenGL3_NewFrame();
ImGui_ImplAndroid_NewFrame(egl.width, egl.height);
ImGui::NewFrame();
int touchCount = (((int (*)())(Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"))))());
if (touchCount > 0) {
UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields(*)(int))(Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.CoreModule.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1)))(0);
float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;
switch (touch.m_Phase) {
case TouchPhase::Began:
case TouchPhase::Stationary:
io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
io.MouseDown[0] = true;
break;
case TouchPhase::Ended:
case TouchPhase::Canceled:
io.MouseDown[0] = false;
clearMousePos = true;
break;case TouchPhase::Moved:
io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
break;default:break;}}
menu();DrawESP(egl.width, egl.height);Aimbott();
ImGui::EndFrame();ImGui::Render();
glViewport(0, 0, io.DisplaySize.x, io.DisplaySize.y);
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
return old_eglSwapBuffers(dpy, surface);
}


void *FreeFire(const char *) {
DBG_INFO("FreeFire(): injected thread started, pid=%d", getpid());
//while (!getLibraryLoaded(OBFUSCATE("libanogs.so")));
sleep(7);
DBG_INFO("FreeFire(): 7s wait done, dlopen libEGL.so");
void* libEGL = dlopen(OBFUSCATE("libEGL.so"), RTLD_NOW | RTLD_GLOBAL);
if (!libEGL) {
    DBG_ERROR("FreeFire(): dlopen(libEGL.so) FAILED — aborting injection");
    return nullptr;
}
DBG_INFO("FreeFire(): libEGL.so loaded @ %p, hooking eglSwapBuffers", libEGL);
A64HookFunction((void *) dlsym(dlopen(OBFUSCATE("libEGL.so"), 4), OBFUSCATE("eglSwapBuffers")), (void *) hook_eglSwapBuffers, (void **) &old_eglSwapBuffers);
// A64HookFunction itself returns void — old_eglSwapBuffers being non-null
// after the call is the actual success signal (the hook sets it before
// returning), so that's what we log instead of a made-up return value.
DBG_INFO("FreeFire(): eglSwapBuffers hook requested (old=%p, %s)", (void*)old_eglSwapBuffers, old_eglSwapBuffers ? "likely OK" : "likely FAILED — old fn ptr still null");
DBG_INFO("FreeFire(): calling Il2CppAttach()");
Il2CppAttach();
DBG_INFO("FreeFire(): Il2CppAttach() returned, assemblies loaded = %s", Il2CppIsAssembliesLoaded() ? "yes" : "no");
//A64HookFunction((void *)Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("PlayerAttributes"), OBFUSCATE("PBDHFOEOIBH"), 1), (void *) _SP, (void **) &SP);
if (mlovinit()){
DBG_INFO("FreeFire(): mlovinit() OK, setting up shader/Wallhack");
setShader(OBFUSCATE("_AlphaMask"));
LogShaders(); Wallhack();
} else {
DBG_ERROR("FreeFire(): mlovinit() FAILED — shader/Wallhack setup skipped");
}
DBG_INFO("FreeFire(): init sequence complete");
return nullptr;
}

void *pLibRealUnity = 0;
typedef jint(JNICALL *CallJNI_OnLoad_t)(JavaVM *vm, void *reserved);
typedef void(JNICALL *CallJNI_OnUnload_t)(JavaVM *vm, void *reserved);
CallJNI_OnLoad_t RealJNIOnLoad = 0; CallJNI_OnUnload_t RealJNIOnUnload = 0;
JNIEXPORT jint JNICALL CallJNIOL(
JavaVM *vm, void *reserved) {
if (!pLibRealUnity)
pLibRealUnity = dlopen(OBFUSCATE("libmainn.so"), RTLD_NOW);
if (!RealJNIOnLoad)
RealJNIOnLoad = reinterpret_cast<CallJNI_OnLoad_t>(dlsym(pLibRealUnity, OBFUSCATE("JNI_OnLoad")));
return RealJNIOnLoad(vm, reserved);
}
JNIEXPORT void JNICALL CallJNIUL(
JavaVM *vm, void *reserved) {
if (!pLibRealUnity)
pLibRealUnity = dlopen(OBFUSCATE("libmainn.so"), RTLD_NOW);
if (!RealJNIOnUnload)
RealJNIOnUnload = reinterpret_cast<CallJNI_OnUnload_t>(dlsym(pLibRealUnity, OBFUSCATE("JNI_OnUnload")));
RealJNIOnUnload(vm, reserved);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
jvm = vm;
return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
CallJNIUL(vm, reserved);
}

__attribute__((constructor))
void lib_main() {
std::thread(FreeFire, nullptr).detach();
}


/*EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
eglQuerySurface(dpy, surface, EGL_WIDTH, &egl.width);
eglQuerySurface(dpy, surface, EGL_HEIGHT, &egl.height);
if (!egl.setup) {
InitTexture();
ImGui::CreateContext();
ImGui::StyleColorsDark();
SetupImgui();
egl.setup = true;
}
ImGuiIO &io = ImGui::GetIO();ImGui_ImplOpenGL3_NewFrame();
ImGui_ImplAndroid_NewFrame(egl.width, egl.height);
ImGui::NewFrame();
int touchCount = (((int (*)())(Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("get_touchCount"))))());
if (touchCount > 0) {
UnityEngine_Touch_Fields touch = ((UnityEngine_Touch_Fields(*)(int))(Il2CppGetMethodOffset(OBFUSCATE("UnityEngine.dll"), OBFUSCATE("UnityEngine"), OBFUSCATE("Input"), OBFUSCATE("GetTouch"), 1)))(0);
float reverseY = io.DisplaySize.y - touch.m_Position.fields.y;
switch (touch.m_Phase) {
case TouchPhase::Began:
case TouchPhase::Stationary:
io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
io.MouseDown[0] = true;
break;
case TouchPhase::Ended:
case TouchPhase::Canceled:
io.MouseDown[0] = false;
clearMousePos = true;
break;case TouchPhase::Moved:
io.MousePos = ImVec2(touch.m_Position.fields.x, reverseY);
break;default:break;}}menu();
ImGui::EndFrame();ImGui::Render();
glViewport(0, 0, io.DisplaySize.x, io.DisplaySize.y);
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
return old_eglSwapBuffers(dpy, surface);
}

void *FreeFire(const char *) {
DBG_INFO("FreeFire(): injected thread started, pid=%d", getpid());
//while (!getLibraryLoaded(OBFUSCATE("libanogs.so")));
sleep(7);
DBG_INFO("FreeFire(): 7s wait done, dlopen libEGL.so");
void* libEGL = dlopen(OBFUSCATE("libEGL.so"), RTLD_NOW | RTLD_GLOBAL);
if (!libEGL) {
    DBG_ERROR("FreeFire(): dlopen(libEGL.so) FAILED — aborting injection");
    return nullptr;
}
DBG_INFO("FreeFire(): libEGL.so loaded @ %p, hooking eglSwapBuffers", libEGL);
A64HookFunction((void *) dlsym(dlopen(OBFUSCATE("libEGL.so"), 4), OBFUSCATE("eglSwapBuffers")), (void *) hook_eglSwapBuffers, (void **) &old_eglSwapBuffers);
// A64HookFunction itself returns void — old_eglSwapBuffers being non-null
// after the call is the actual success signal (the hook sets it before
// returning), so that's what we log instead of a made-up return value.
DBG_INFO("FreeFire(): eglSwapBuffers hook requested (old=%p, %s)", (void*)old_eglSwapBuffers, old_eglSwapBuffers ? "likely OK" : "likely FAILED — old fn ptr still null");
DBG_INFO("FreeFire(): calling Il2CppAttach()");
Il2CppAttach();
DBG_INFO("FreeFire(): Il2CppAttach() returned, assemblies loaded = %s", Il2CppIsAssembliesLoaded() ? "yes" : "no");
//A64HookFunction((void *)Il2CppGetMethodOffset(OBFUSCATE("Assembly-CSharp.dll"), OBFUSCATE("COW.GamePlay"), OBFUSCATE("PlayerAttributes"), OBFUSCATE("PBDHFOEOIBH"), 1), (void *) _SP, (void **) &SP);
if (mlovinit()){
DBG_INFO("FreeFire(): mlovinit() OK, setting up shader/Wallhack");
setShader(OBFUSCATE("_AlphaMask"));
LogShaders(); Wallhack();
} else {
DBG_ERROR("FreeFire(): mlovinit() FAILED — shader/Wallhack setup skipped");
}
DBG_INFO("FreeFire(): init sequence complete");
return nullptr;
}


void *pLibRealUnity = 0;
typedef jint(JNICALL *CallJNI_OnLoad_t)(JavaVM *vm, void *reserved);
typedef void(JNICALL *CallJNI_OnUnload_t)(JavaVM *vm, void *reserved);
CallJNI_OnLoad_t RealJNIOnLoad = 0; CallJNI_OnUnload_t RealJNIOnUnload = 0;
JNIEXPORT jint JNICALL CallJNIOL(
JavaVM *vm, void *reserved) {
if (!pLibRealUnity)
pLibRealUnity = dlopen(OBFUSCATE("libmainn.so"), RTLD_NOW);
if (!RealJNIOnLoad)
RealJNIOnLoad = reinterpret_cast<CallJNI_OnLoad_t>(dlsym(pLibRealUnity, OBFUSCATE("JNI_OnLoad")));
return RealJNIOnLoad(vm, reserved);
}
JNIEXPORT void JNICALL CallJNIUL(
JavaVM *vm, void *reserved) {
if (!pLibRealUnity)
pLibRealUnity = dlopen(OBFUSCATE("libmainn.so"), RTLD_NOW);
if (!RealJNIOnUnload)
RealJNIOnUnload = reinterpret_cast<CallJNI_OnUnload_t>(dlsym(pLibRealUnity, OBFUSCATE("JNI_OnUnload")));
RealJNIOnUnload(vm, reserved);
}
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
jvm = vm;
return CallJNIOL(vm, reserved);
}
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
CallJNIUL(vm, reserved);
}

__attribute__((constructor))
void lib_main() {
std::thread(FreeFire, nullptr).detach();
}*/




