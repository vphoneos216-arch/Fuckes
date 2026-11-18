#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"
#include "Pubg_Font.h"
#include "gsicons.h"
#include "SPECIAL.h"
#include "MAIN_FONT.h"
#include "Iconcpp.h"
#include "lundbda.h"
//#include "rohit.h"
#include "ICONS.h"
#include <imgui.h>
#include <memory.h>
bool Speed = true;
float (*EXEMPLO) (void* as);
float _EXEMPLO(void* as){
if (Speed){
return 1.9f;
}
return EXEMPLO(as);
}

ImFont* Itsmk = nullptr;
ImFont* largeFont = nullptr;

struct {
    bool setup;
    int width;
    int height;
    int screenWidth;
    int screenHeight;
} egl;

void (*old_input)(void *event, void *exAb, void *exAc);
void hook_input(void *event, void *exAb, void *exAc) {
old_input(event, exAb, exAc);
ImGui_ImplAndroid_HandleTouchEvent((AInputEvent *) event, {(float) egl.screenWidth / (float) egl.width, (float) egl.screenHeight / (float) egl.height});
return;
}
int (*old_getWidth)(ANativeWindow* window);
int hook_getWidth(ANativeWindow* window) {
egl.screenWidth = old_getWidth(window);
return old_getWidth(window);
}
int (*old_getHeight)(ANativeWindow* window);
int hook_getHeight(ANativeWindow* window) {
egl.screenHeight = old_getHeight(window);
return old_getHeight(window);
}

float density = -1;
//ImFont *font;


inline static bool g_IsSetup = false;
inline int prevWidth, prevHeight;
ImFontConfig config;
void SetupImgui() {
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGui_ImplOpenGL3_Init("#version 300 es");

ImGuiIO &io = ImGui::GetIO();
            
//Set ImGui Style
ImGuiStyle *style = &ImGui::GetStyle();
// --- General Styling and Spacing ---
style->WindowPadding = ImVec2(10, 10);
style->WindowRounding = 6.0f;
style->FramePadding = ImVec2(10, 8);
style->ItemSpacing = ImVec2(8, 8);
style->ItemInnerSpacing = ImVec2(8, 6);
style->FrameRounding = 2.0f;
style->ChildRounding = 2.0f;
style->WindowBorderSize = 2.0f;
style->FrameBorderSize = 0.5f;
style->PopupRounding = 8.0f;
style->ScrollbarSize = 20.0f;
style->ScrollbarRounding = 6.0f;
style->GrabRounding = 6.0f;
style->GrabMinSize = 10.0f;
style->TabRounding = 2.0f;
style->TabBorderSize = 0.0f;
style->ButtonTextAlign = ImVec2(0.5f, 0.5f);
style->WindowTitleAlign = ImVec2(0.5f, 0.5f);

// --- iOS-style Color Palette ---
ImVec4* colors = style->Colors;

// Backgrounds
colors[ImGuiCol_WindowBg]   = ImColor(28, 28, 30, 255); // Dark gray window background
colors[ImGuiCol_ChildBg]    = ImColor(0, 0, 0, 0); // Transparent child background
colors[ImGuiCol_PopupBg]    = ImColor(44, 44, 46, 255); // Slightly lighter for popups
colors[ImGuiCol_MenuBarBg]  = ImColor(28, 28, 30, 255);

// Text
colors[ImGuiCol_Text]       = ImColor(255, 255, 255, 255); // White text
colors[ImGuiCol_TextDisabled]= ImColor(174, 174, 178, 255); // A softer gray for disabled text

// Frames (inputs, buttons)
colors[ImGuiCol_FrameBg]    = ImColor(58, 58, 60, 255); // Dark gray frame background
colors[ImGuiCol_FrameBgHovered] = ImColor(70, 70, 75, 255);
colors[ImGuiCol_FrameBgActive]  = ImColor(85, 85, 90, 255);

// Headers and Collapsing Headers
colors[ImGuiCol_Header]     = ImColor(10, 132, 255, 25); // Subtle iOS blue header
colors[ImGuiCol_HeaderHovered]  = ImColor(10, 132, 255, 50);
colors[ImGuiCol_HeaderActive]   = ImColor(10, 132, 255, 75);

// Buttons
colors[ImGuiCol_Button]     = ImColor(10, 132, 255, 255); // Solid iOS blue
colors[ImGuiCol_ButtonHovered]  = ImColor(10, 132, 255, 200); // Slight transparency on hover
colors[ImGuiCol_ButtonActive]   = ImColor(10, 132, 255, 150);

// Selection and Highlights
colors[ImGuiCol_CheckMark]  = ImColor(10, 132, 255, 255);
colors[ImGuiCol_SliderGrab] = ImColor(10, 132, 255, 255);
colors[ImGuiCol_SliderGrabActive]   = ImColor(10, 132, 255, 200);
colors[ImGuiCol_TextSelectedBg] = ImColor(10, 132, 255, 75);
colors[ImGuiCol_NavHighlight]   = ImColor(10, 132, 255, 255);

// Borders and separators
colors[ImGuiCol_Border]     = ImColor(10, 132, 255, 255);
colors[ImGuiCol_Separator]  = ImColor(85, 85, 90, 255);

// Scrollbars
colors[ImGuiCol_ScrollbarBg]= ImColor(44, 44, 46, 250);
colors[ImGuiCol_ScrollbarGrab]  = ImColor(100, 100, 105, 250);
colors[ImGuiCol_ScrollbarGrabHovered]   = ImColor(120, 120, 125, 250);
colors[ImGuiCol_ScrollbarGrabActive]= ImColor(150, 150, 155, 250);
colors[ImGuiCol_DragDropTarget] = ImColor(10, 132, 255, 255);

// Tabs
colors[ImGuiCol_Tab]        = ImColor(28, 28, 30, 255);
colors[ImGuiCol_TabHovered] = ImColor(10, 132, 255, 255);
colors[ImGuiCol_TabActive]  = ImColor(10, 132, 255, 255);
colors[ImGuiCol_TabUnfocused]= ImColor(28, 28, 30, 255);
colors[ImGuiCol_TabUnfocusedActive] = ImColor(10, 132, 255, 150);

// Titles
colors[ImGuiCol_TitleBg]    = ImColor(28, 28, 30, 255);
colors[ImGuiCol_TitleBgActive]  = ImColor(28, 28, 30, 255);
colors[ImGuiCol_TitleBgCollapsed]   = ImColor(28, 28, 30, 255);

// Resizing
colors[ImGuiCol_ResizeGrip] = ImColor(100, 100, 105, 255);
colors[ImGuiCol_ResizeGripHovered]  = ImColor(120, 120, 125, 255);
colors[ImGuiCol_ResizeGripActive]   = ImColor(150, 150, 155, 255);

// Navigations
colors[ImGuiCol_NavHighlight]   = ImColor(10, 132, 255, 255);
colors[ImGuiCol_NavWindowingHighlight]  = ImColor(200, 200, 205, 180);
colors[ImGuiCol_NavWindowingDimBg]  = ImColor(200, 200, 205, 20);
colors[ImGuiCol_ModalWindowDimBg]   = ImColor(200, 200, 205, 35);


  static const ImWchar icons_ranges[] = {0xf000, 0xf3ff, 0};
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.PixelSnapH = true;
		icons_config.OversampleH = 2.5;
		icons_config.OversampleV = 2.5;
		ImFontConfig CustomFont;
  CustomFont.FontDataOwnedByAtlas = false;

//   io.Fonts->AddFontFromMemoryTTF((void *)PIRO_data, PIRO_size, 28.0f, NULL, io.Fonts->GetGlyphRangesChineseFull()); 
	    io.Fonts->AddFontFromMemoryTTF((void *)default_f, sizeof default_f, 30, NULL, io.Fonts->GetGlyphRangesCyrillic());
//		 io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(itsmkop), sizeof(itsmkop), 30.f, &CustomFont);
		 //io.Fonts->AddFontFromMemoryTTF((void *)BaiduZY_data, BaiduZY_size, 30.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
		  io.Fonts->AddFontFromMemoryCompressedTTF(font_awesome_data, font_awesome_size, 30.0f, &icons_config, icons_ranges);
		  Itsmk = io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(itsmkop), sizeof(itsmkop), 30.f, &CustomFont);
		  largeFont = io.Fonts->AddFontFromMemoryTTF((void *)F107_data, F107_size, 32.0f, NULL, io.Fonts->GetGlyphRangesDefault());
          
         // memset(&Config, 0, sizeof(sConfig));
//

//ImFontConfig CustomFont;
//CustomFont.FontDataOwnedByAtlas = false;
//font_cfg.SizePixels = 22.0f;
/*statics const ImWchar vietnamese_chars[] = {
0x0020, 0x00FF, // Basic Latin + Latin Supplement
0x0102, 0x0103, // Â, â
0x0110, 0x0111, // Đ, đ
0x0128, 0x0129, // Ĩ, ĩ
0x0168, 0x0169, // Ũ, ũ
0x01A0, 0x01A1, // Ơ, ơ
0x01AF, 0x01B0, // Ư, ư
0x1EA0, 0x1EF9, // Vietnamese specific characters
0 // null-terminated list
};*/
}

void DrawTextCentered(const char *text)
{
ImGui::Separator();
ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(text).x) / 2.f);
ImGui::Text(text);
ImGui::Separator();
}

void DrawText(std::string text) {
ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(text.c_str()).x) / 2.f);
ImGui::Text(text.c_str());
}

struct UnityEngine_Vector2_Fields {
float x;
float y;
};

struct UnityEngine_Vector2_o {
UnityEngine_Vector2_Fields fields;
};

enum TouchPhase {
Began = 0,
Moved = 1,
Stationary = 2,
Ended = 3,
Canceled = 4
};

struct UnityEngine_Touch_Fields {
int32_t m_FingerId;
struct UnityEngine_Vector2_o m_Position;
struct UnityEngine_Vector2_o m_RawPosition;
struct UnityEngine_Vector2_o m_PositionDelta;
float m_TimeDelta;
int32_t m_TapCount;
int32_t m_Phase;
int32_t m_Type;
float m_Pressure;
float m_maximumPossiblePressure;
float m_Radius;
float m_RadiusVariance;
float m_AltitudeAngle;
float m_AzimuthAngle;
};




