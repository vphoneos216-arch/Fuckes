#include "Android_draw/draw.h"
#include <chrono>
// Source project fonts (方正粗等线 + icomoon icons)
#include "UI/fonts/YuluFont.h"
#include "UI/fonts/YuluFontIcons.h"

EGLDisplay display = EGL_NO_DISPLAY;
EGLConfig config;
EGLSurface surface = EGL_NO_SURFACE;
EGLContext context = EGL_NO_CONTEXT;

ANativeWindow *native_window;

int native_window_screen_x = 0;
int native_window_screen_y = 0;
android::ANativeWindowCreator::DisplayInfo displayInfo{0};
uint32_t orientation = 0;
bool g_Initialized = false;
ImGuiWindow *g_window = nullptr;

bool initGUI_draw(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    orientation = displayInfo.orientation;

    #if defined(USE_OPENGL)
        if (!init_egl(_screen_x, _screen_y, log)) {
            return false;
        }
    #else
        InitVulkan();
        SetupVulkan();
        ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, false);
        SetupVulkanWindow(::native_window, (int) _screen_x, (int) _screen_y);
    #endif
    if (!ImGui_init()) {
        return false;
    }   

    #ifndef USE_OPENGL
        UploadFonts();
    #endif
     
    return true;
}

bool init_egl(uint32_t _screen_x, uint32_t _screen_y, bool log) {
    ::native_window = android::ANativeWindowCreator::Create("AImGui", _screen_x, _screen_y, false);

    ANativeWindow_acquire(native_window);
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return false;
    }
    if (log) {
    }
    if (eglInitialize(display, 0, 0) != EGL_TRUE) {
        return false;
    }
    if (log) {
    }
    EGLint num_config = 0;
    const EGLint attribList[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_RED_SIZE, 5,
        EGL_BUFFER_SIZE, 32,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint attrib_list[] = {
        EGL_CONTEXT_CLIENT_VERSION,
        3,
        EGL_NONE
    };
    
    if (eglChooseConfig(display, attribList, &config, 1, &num_config) != EGL_TRUE) {
        return false;
    }
    EGLint egl_format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &egl_format);
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, egl_format);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, attrib_list);
    if (context == EGL_NO_CONTEXT) {
        return false;
    }
    surface = eglCreateWindowSurface(display, config, native_window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        return false;
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        return false;
    }
    return true;
}

// Every GetDisplayInfo() is a binder call into SurfaceFlinger. The render
// loop calls this several times per frame (~70 fps), which was hundreds of
// IPC calls per second: the main cause of phone heat and game stutter. The
// screen size and rotation change rarely, so refresh at most 4 times a second.
void screen_config() {
    static std::chrono::steady_clock::time_point lastQuery{};
    static bool queried = false;
    const auto now = std::chrono::steady_clock::now();
    if (queried && now - lastQuery < std::chrono::milliseconds(250)) return;
    queried   = true;
    lastQuery = now;
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

bool ImGui_init() {
    if (g_Initialized) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // ── PREMIUM RED + BLACK THEME ────────────────────────────
    // Applies to ImGui's built-in widgets used by the menu pages
    // (SliderFloat, RadioButton, Button, ScrollBar, child frames).
    // Only colours are set here — every size/rounding/spacing value
    // below this block is left exactly as the layout requires.
    ImGui::StyleColorsDark();
    {
        ImVec4* c = ImGui::GetStyle().Colors;
        const ImVec4 RED      = ImVec4(1.00f, 0.176f, 0.176f, 1.00f);  // #FF2D2D
        const ImVec4 RED_HOV  = ImVec4(1.00f, 0.302f, 0.302f, 1.00f);  // #FF4D4D
        const ImVec4 RED_ACT  = ImVec4(0.667f, 0.086f, 0.086f, 1.00f); // #AA1616
        const ImVec4 BLACK    = ImVec4(0.051f, 0.051f, 0.051f, 1.00f); // #0D0D0D
        const ImVec4 SURF     = ImVec4(0.102f, 0.102f, 0.102f, 1.00f); // #1A1A1A
        const ImVec4 IDLE     = ImVec4(0.149f, 0.149f, 0.149f, 1.00f); // #262626
        const ImVec4 WHITE    = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        const ImVec4 GREY     = ImVec4(0.647f, 0.647f, 0.647f, 1.00f);

        c[ImGuiCol_Text]                 = WHITE;
        c[ImGuiCol_TextDisabled]         = GREY;
        c[ImGuiCol_WindowBg]             = BLACK;
        c[ImGuiCol_ChildBg]              = ImVec4(0,0,0,0);
        c[ImGuiCol_PopupBg]              = SURF;
        c[ImGuiCol_Border]               = ImVec4(0.176f,0.176f,0.176f,1.00f);
        c[ImGuiCol_BorderShadow]         = ImVec4(0,0,0,0);
        c[ImGuiCol_FrameBg]              = IDLE;
        c[ImGuiCol_FrameBgHovered]       = ImVec4(0.220f,0.098f,0.098f,1.00f);
        c[ImGuiCol_FrameBgActive]        = RED_ACT;
        c[ImGuiCol_TitleBg]              = BLACK;
        c[ImGuiCol_TitleBgActive]        = SURF;
        c[ImGuiCol_TitleBgCollapsed]     = BLACK;
        c[ImGuiCol_MenuBarBg]            = SURF;
        c[ImGuiCol_ScrollbarBg]          = ImVec4(0,0,0,0);
        c[ImGuiCol_ScrollbarGrab]        = IDLE;
        c[ImGuiCol_ScrollbarGrabHovered] = RED_HOV;
        c[ImGuiCol_ScrollbarGrabActive]  = RED;
        c[ImGuiCol_CheckMark]            = RED;
        c[ImGuiCol_SliderGrab]           = RED;
        c[ImGuiCol_SliderGrabActive]     = RED_HOV;
        c[ImGuiCol_Button]               = IDLE;
        c[ImGuiCol_ButtonHovered]        = RED_HOV;
        c[ImGuiCol_ButtonActive]         = RED;
        c[ImGuiCol_Header]               = IDLE;
        c[ImGuiCol_HeaderHovered]        = RED_HOV;
        c[ImGuiCol_HeaderActive]         = RED;
        c[ImGuiCol_Separator]            = ImVec4(0.176f,0.176f,0.176f,1.00f);
        c[ImGuiCol_SeparatorHovered]     = RED_HOV;
        c[ImGuiCol_SeparatorActive]      = RED;
        c[ImGuiCol_ResizeGrip]           = IDLE;
        c[ImGuiCol_ResizeGripHovered]    = RED_HOV;
        c[ImGuiCol_ResizeGripActive]     = RED;
        c[ImGuiCol_Tab]                  = IDLE;
        c[ImGuiCol_TabHovered]           = RED_HOV;
        c[ImGuiCol_TabActive]            = RED;
        c[ImGuiCol_PlotLines]            = RED;
        c[ImGuiCol_PlotLinesHovered]     = RED_HOV;
        c[ImGuiCol_PlotHistogram]        = RED;
        c[ImGuiCol_PlotHistogramHovered] = RED_HOV;
        c[ImGuiCol_TextSelectedBg]       = ImVec4(1.00f,0.176f,0.176f,0.35f);
        c[ImGuiCol_DragDropTarget]       = RED;
        c[ImGuiCol_NavHighlight]         = RED;
        c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0,0,0,0.60f);
    }
    ImGui_ImplAndroid_Init(native_window);
    #if defined(USE_OPENGL)
        ImGui_ImplOpenGL3_Init("#version 300 es");
    #endif
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    style->WindowTitleAlign = ImVec2(0.5, 0.5);
    style->PopupRounding = 3;
    style->WindowPadding = ImVec2(4, 4);
    style->FramePadding  = ImVec2(2, 2);
    style->ItemSpacing   = ImVec2(2, 2);
    style->ScrollbarSize = 10;
    style->WindowBorderSize = 3;
    style->ChildBorderSize  = 1;
    style->PopupBorderSize  = 1;
    style->FrameBorderSize  = 1;
    style->WindowRounding    = 3;
    style->ChildRounding     = 3;
    style->FrameRounding     = 3;
    style->ScrollbarRounding = 2;
    style->GrabRounding      = 3;
    
    io.IniFilename = NULL;
    // Load source font: 方正粗等线 (raw TTF bytes)
    // FontDataOwnedByAtlas=false → ImGui won't try to free the static array
    ImFontConfig font_main_cfg;
    font_main_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(
        (void *)雨落_data, (int)雨落_size, 21.0f,
        &font_main_cfg, io.Fonts->GetGlyphRangesChineseFull());

    // Merge icomoon icon font (private use area E000-F8FF)
    ImFontConfig cfg_icon;
    cfg_icon.MergeMode = true;
    cfg_icon.FontDataOwnedByAtlas = false;
    static ImWchar icon_ranges[] = { 0xE000, 0xF8FF, 0 };
    io.Fonts->AddFontFromMemoryTTF(
        (void *)雨落图标_data, (int)雨落图标_size, 21.0f,
        &cfg_icon, icon_ranges);

    io.Fonts->Build();
    ::g_Initialized = true;
    return true;
}

void drawBegin() {
    screen_config();
    if (::orientation != displayInfo.orientation) {
        ::orientation = displayInfo.orientation;
        UpdateScreenData(displayInfo.width, displayInfo.height, displayInfo.orientation);
    }  // ← This closing brace was missing!

    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_NewFrame();
    #else
        ImGui_ImplVulkan_NewFrame();
    #endif        
    ImGui_ImplAndroid_NewFrame(native_window_screen_x, native_window_screen_y);
    ImGui::NewFrame();
}  // ← drawBegin closing brace

void drawEnd() {
    ImGui::Render();
    
    #ifdef USE_OPENGL
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        eglSwapBuffers(display, surface);
    #else
        FrameRender(ImGui::GetDrawData());
        FramePresent();
    #endif
}  // ← drawEnd closing brace

void shutdown() {
    if (!g_Initialized) {
        return;
    }
    // Cleanup
    #ifdef USE_OPENGL
        ImGui_ImplOpenGL3_Shutdown();
    #else
        DeviceWait();
        ImGui_ImplVulkan_Shutdown();
    #endif
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    
    #ifdef USE_OPENGL
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) {
                eglDestroyContext(display, context);
            }
            if (surface != EGL_NO_SURFACE) {
                eglDestroySurface(display, surface);
            }
            eglTerminate(display);
        }
        display = EGL_NO_DISPLAY;
        context = EGL_NO_CONTEXT;
        surface = EGL_NO_SURFACE;
    #else    
        CleanupVulkanWindow();
        CleanupVulkan();
    #endif
    
    ANativeWindow_release(native_window);
    android::ANativeWindowCreator::Destroy(native_window);
    ::g_Initialized = false;
}  // ← shutdown closing brace