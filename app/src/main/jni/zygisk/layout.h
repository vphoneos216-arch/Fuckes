#include <imports.h>
#include "BatteryInfo.h"
#include <imguiconfig.h>
// [UPDATED] layout.h uses std::map (below) but never included <map> itself —
// confirmed from the actual CI log: "no template named 'map' in namespace 'std'".
#include <map>

#include "ItSRohitOp.h"
#include "exptime.h"
#include "Icon.h"
void (*OpenURL)(String *url);
static int language = 0;
float SIZE_REDUCE = 0.f;
bool itsmk;
ImTextureID iconAim; // ícone do olho
ImTextureID iconEsp; // ícone da caveira
//ImFont* Itsmk = nullptr;



#include <algorithm>
#include <string>
#include <sys/system_properties.h>

#ifndef PROP_VALUE_MAX
#define PROP_VALUE_MAX 92
#endif

std::string getDeviceFullName() {
    char brandC[PROP_VALUE_MAX], modelC[PROP_VALUE_MAX];
    __system_property_get("ro.product.brand", brandC);
    __system_property_get("ro.product.model", modelC);
    std::string brand(brandC);
    std::string model(modelC);
    return brand + " " + model;
}





struct TabAnimationState {
float lerped_alpha = 0.0f;
float lerped_bar_width = 0.0f;
};
std::map<ImGuiID, TabAnimationState> g_tab_animations;

bool CustomAnimatedTabButton(const char* id_label, ImTextureID image_id, bool is_active, float button_width, float button_height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    // ... (unchanged animation and background drawing logic) ...
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(id_label);
    const ImVec2 pos = window->DC.CursorPos;
    const float rounding = 2.0f;
    const ImRect bb(ImVec2(pos.x, pos.y), ImVec2(pos.x + button_width, pos.y + button_height));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    // Find or create the animation state for this button.
    auto it_anim = g_tab_animations.find(id);
    if (it_anim == g_tab_animations.end()) {
        g_tab_animations.insert({ id, TabAnimationState() });
        it_anim = g_tab_animations.find(id);
    }
    float target_lerp_value = is_active ? 1.0f : 0.0f;
    float lerp_speed = 8.0f * ImGui::GetIO().DeltaTime;
    it_anim->second.lerped_alpha = ImLerp(it_anim->second.lerped_alpha, target_lerp_value, lerp_speed);
    ImU32 bg_color_inactive = IM_COL32(58, 58, 60, 255);
    ImU32 bg_color_hovered = IM_COL32(70, 70, 75, 255);
    ImU32 bg_color_active = IM_COL32(44, 44, 46, 255);
    ImVec4 final_bg_color_v4;
    if (is_active) {
        final_bg_color_v4 = ImColor(bg_color_active).Value;
    }
    else if (hovered) {
        final_bg_color_v4 = ImColor(bg_color_hovered).Value;
    }
    else {
        final_bg_color_v4 = ImColor(bg_color_inactive).Value;
    }
    ImU32 current_bg_color = ImGui::ColorConvertFloat4ToU32(ImLerp(ImColor(bg_color_inactive).Value, final_bg_color_v4, it_anim->second.lerped_alpha));
    window->DrawList->AddRectFilled(bb.Min, bb.Max, current_bg_color, rounding);

    // --- Draw the image here with white color tint ---
    ImVec2 image_size = ImVec2(50.0f, 50.0f); // Adjust as needed
    ImVec2 image_pos = ImVec2(bb.Min.x + (button_width - image_size.x) * 0.5f, bb.Min.y + (button_height - image_size.y) * 0.5f);

    // The key change is adding IM_COL32_WHITE as the tint_color parameter
    window->DrawList->AddImage(image_id, image_pos, ImVec2(image_pos.x + image_size.x, image_pos.y + image_size.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    // You could also use ImColor(255, 255, 255, 255) for the same effect.


    if (is_active) {
        window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(10, 132, 255, 205), rounding, ImDrawFlags_None, 0.2f);
    }

    return pressed;
}

static std::map<ImGuiID, float> s_anim_pos;


bool MyCustomCheckbox(const char* label, bool* state) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImVec2 label_size = ImGui::CalcTextSize(label);
    
    const float toggle_height = label_size.y * 1.2f;
    const float toggle_width = toggle_height * 2.0f;
    const float radius = toggle_height * 0.5f;

    float total_width = ImGui::GetContentRegionAvail().x;
    ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + total_width, window->DC.CursorPos.y + toggle_height));

    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    if (pressed) {
        *state = !*state;
    }

    float t_state = *state ? 1.0f : 0.0f;
    
    if (s_anim_pos.find(id) == s_anim_pos.end()) {
        s_anim_pos[id] = t_state;
    }
    s_anim_pos[id] = ImLerp(s_anim_pos[id], t_state, g.IO.DeltaTime * 8.0f);
    float current_pos = s_anim_pos[id];

    // --- Drawing Logic ---
    
    // Define colors for ON and OFF states
    ImU32 frame_color_off = IM_COL32(225, 225, 225, 255); // Light gray frame for off state
    ImU32 frame_color_on = IM_COL32(0, 185, 255, 255);
    
    // Updated grab color for the 'off' state to be a dark gray
    ImU32 grab_color_off = IM_COL32(250, 250, 250, 255); 
    ImU32 grab_color_on = IM_COL32(255, 255, 255, 255);
    
    ImU32 bg_color = ImGui::ColorConvertFloat4ToU32(ImLerp(ImColor(frame_color_off).Value, ImColor(frame_color_on).Value, current_pos));
    ImU32 knob_color = ImGui::ColorConvertFloat4ToU32(ImLerp(ImColor(grab_color_off).Value, ImColor(grab_color_on).Value, current_pos));

    ImVec2 text_pos(bb.Min.x, bb.Min.y + (toggle_height - label_size.y) * 0.5f);
    window->DrawList->AddText(text_pos, IM_COL32(255, 255, 255, 255), label);
    
    ImVec2 toggle_min(bb.Max.x - toggle_width, bb.Min.y);
    ImVec2 toggle_max(bb.Max.x, bb.Min.y + toggle_height);
    window->DrawList->AddRectFilled(toggle_min, toggle_max, bg_color, toggle_height * 0.5f);
    
    float knob_size = radius - 2.5f;
    ImVec2 circle_pos(toggle_min.x + radius + current_pos * (toggle_width - 2.0f * radius), toggle_min.y + radius);
    
    window->DrawList->AddCircleFilled(circle_pos, knob_size + 1.5f, IM_COL32(0, 0, 0, 50));
    
    window->DrawList->AddCircleFilled(circle_pos, knob_size, knob_color);

    return pressed;
}


void menu() {


static bool itsmk = true;
static bool clickInProgress = false;
static float clickStartTime = 0;
if (itsmk == false) {
if (ImGui::Begin((" MKOP" ), 0,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
const auto& pWindowDrawList = ImGui::GetWindowDrawList();
const auto& p = ImGui::GetWindowPos();
ImGui::SetCursorPos(ImVec2(5, 5));
ImVec2 imageSize(120,120);
ImGui::Image(logo.textureId, imageSize);
if (ImGui::IsItemHovered()) {
if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {clickInProgress = true;
clickStartTime = ImGui::GetTime();}}
if (clickInProgress && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {clickInProgress = false;
float currentTime = ImGui::GetTime();
float clickDuration = currentTime - clickStartTime;
if (clickDuration < 0.2f) {itsmk = true;
}}}}

ImGui::End();


if (itsmk){
ImGui::SetNextWindowSize(ImVec2(580, 500), ImGuiCond_Always);
    ImGui::Begin("##MANI PAPA IS BACK", nullptr,
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoTitleBar);

    
  /*     static bool isLogin = false;

if (!isLogin) {
ImGui::Spacing();
ImGui::Text(OBFUSCATE(ICON_FA_STAR " MOD BY POWERCHEAT  " ICON_FA_CHECK_CIRCLE));
ImGui::Spacing();

ImGui::Text(ICON_FA_PASTE " LOGIN TO CONTINUE");
ImGui::Spacing();
ImGui::PushItemWidth(-1);
static char s[64];
ImGui::InputText("##key", s, sizeof s);
ImGui::PopItemWidth();
if (ImGui::Button(ICON_FA_KEYBOARD " PASTE KEY", ImVec2(ImGui::GetContentRegionAvailWidth(), 55))) {
auto key = getClipboard();
strncpy(s, key.c_str(), sizeof s); }
static std::string err;
if (ImGui::Button(ICON_FA_KEY " LOGIN TO CONTINUE " ICON_FA_CHECK_CIRCLE, ImVec2(ImGui::GetContentRegionAvailWidth(), 55))) {
 err = Login(s);
if (err == "OK") {
isLogin = bValid && g_Auth == g_Token;
} }
if (!err.empty() && err != "OK") {
ImGui::Text(ICON_FA_SERVER "SERVER: %s", err.c_str());
}} else {
     */
float btnSize = 40.0f;
       ImGui::SameLine();
       ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - btnSize - 10, 10));
        if (ImGui::InvisibleButton("MinimizeBtn", ImVec2(btnSize, btnSize))) {
           itsmk = false;
        }
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            IM_COL32(10, 177, 255, 180), 4.0f); 
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(ImGui::GetItemRectMin().x + 4, ImGui::GetItemRectMax().y - 5),
            ImVec2(ImGui::GetItemRectMax().x - 4, ImGui::GetItemRectMax().y - 5),
            IM_COL32(255, 255, 255, 255), 2.0f);  
            
			ImGui::SetCursorPos(ImVec2(20, 15));
	
 
//ImGui::SetWindowPos(ImVec2(10, 10));
ImGui::TextColored(ImColor(255, 255, 255, 255),  " !! ZEON MODS  OFFICIAL !! "  );


ImGui::Separator();

ImVec2 windowSize = ImGui::GetWindowSize();
static int tab = 1;
if (ImGui::Button(" AIM MENU ", ImVec2(ImVec2(windowSize.x * 0.318f, 0))))
tab = 1;
ImGui::SameLine();
if (ImGui::Button(" ESP MENU  ", ImVec2(ImVec2(windowSize.x * 0.318f, 0))))
tab = 2;
ImGui::SameLine();
if (ImGui::Button(" INFO ", ImVec2(ImVec2(windowSize.x * 0.318f, 0))))
tab = 3;

ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
ImGui::PushStyleColor(ImGuiCol_Border, ImColor(70, 70, 70, 200).Value);
ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 20.0f);
ImGui::BeginChild("HEX BLADE", ImVec2(0, 0), true);
ImGui::PopStyleVar(3);
if (tab == 1) {
	
	ImGui::TextColored(ImColor(255, 255, 255, 255),"ESP MENU SETTINGS");

MyCustomCheckbox(OBFUSCATE(ICON_FA_GG_CIRCLE" ESP Health"), &Vida);
MyCustomCheckbox(OBFUSCATE(ICON_FA_GG_CIRCLE" ESP Name"), &NewInfoPlayer);
MyCustomCheckbox(OBFUSCATE(ICON_FA_GG_CIRCLE" ESP Cross New"), &Crosshair);
MyCustomCheckbox(OBFUSCATE(ICON_FA_GG_CIRCLE" ESP Crosshair"), &Hexagono);
const char* boxTypeNames[] = {
"Corners",
"Simple Rect",
"Gradient",
"Changing Color"
};
ImGui::Combo("Box Types", &currentBoxType, boxTypeNames, IM_ARRAYSIZE(boxTypeNames));

}
	
if (tab == 2) {
	ImGui::TextColored(ImColor(255, 255, 255, 255),"AIM MENU SETTINGS");
ImGui::Dummy(ImVec2(0, 2));
if (ImGui::Button(AimbotEsp ? ICON_FA_CHECK_SQUARE "  Functions  |  Activated....." : ICON_FA_EXCLAMATION_TRIANGLE "  Functions  |  Deactivated.....", ImVec2(ImGui::GetContentRegionAvailWidth(), 60))) {
AimbotEsp = !AimbotEsp; // Toggle the master variable
}

ImGui::Dummy(ImVec2(0, 2));
DrawTextCentered(ICON_FA_COGS "   Aim Menu   " ICON_FA_COGS);
MyCustomCheckbox(OBFUSCATE("Enable Aimbot"), &aimbotAim);
MyCustomCheckbox(OBFUSCATE("Firing"), &aimbotTrigger);
ImGui::SliderFloat(OBFUSCATE("Aim Circle##AimFov"), &Fov_Aim, 0.0f, 1660.0f, "FOV Size %.0f°");
ImGui::SliderFloat(OBFUSCATE("Config Dis##AimFov"), &Aimdis, 0.0f, 1660.0f, "FOV Dis %.1f M°");
//const char* boxTypeNames[] = {"ANY(Scope/Fire)", "Shooting", "Scoping"};
//ImGui::Combo("Aim - Triggered", &AimWhen, boxTypeNames, IM_ARRAYSIZE(boxTypeNames));
        static const char* aimOptions[] = {  
        OBFUSCATE("HEAD"),  
        OBFUSCATE("NECK"),  
        OBFUSCATE("CHEST"),  
        OBFUSCATE("HIP")  
        };  
            // Variável para armazenar a posição selecionada  
        static int aimPosition = 0; // 0 = HEAD, 1 = NECK, 2 = CHEST, 3 = HIPS  
           // Combo para selecionar a posição do AIM  
        if (ImGui::Combo(OBFUSCATE("AIM LOCAL"), &aimPosition, aimOptions, IM_ARRAYSIZE(aimOptions))) {  
            // Aqui você pode chamar uma função se quiser, ou apenas usar aimPosition nas suas lógicas  
           // Exemplo direto sem switch/case:  
        if (aimPosition == 0) {  
           // HEAD  
          // ex: setAimBone(HEAD_BONE_ID);  
        } else if (aimPosition == 1) {  
          // NECK  
        } else if (aimPosition == 2) {  
          // CHEST  
        } else if (aimPosition == 3) {  
          // HIPS  
        }  
 


}
	

if (tab == 3) {

	ImGui::TextColored(ImColor(255, 255, 255, 255),"INFO ");
	  ImGui::Text(OBFUSCATE("YOUR ADD YT CHANNL️️"));
	}
ImGui::EndChild();
ImGui::PopStyleColor(1);

}


ImGui::End();
}
}

