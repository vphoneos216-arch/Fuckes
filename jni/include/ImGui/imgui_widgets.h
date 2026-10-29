#ifndef __IMGUI_WHITECLOUD_H
#define __IMGUI_WHITECLOUD_H
#include <string>
#include <iostream>
#include <cmath>
#include <ctime>
#include <map>
#include <linux/input.h>
#include "imgui.h"
#include "imgui_internal.h"

void CenteredText(const char* text, float size = 1.5f, ImU32 color = IM_COL32(0, 0, 0, 255))
{
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImFont* font = ImGui::GetFont();
float baseFontSize = font->FontSize;
float scaledFontSize = baseFontSize * size;
ImVec2 textSize = font->CalcTextSizeA(scaledFontSize, FLT_MAX, 0.0f, text);
float availWidth = ImGui::GetContentRegionAvail().x;
ImVec2 cursorPos = ImGui::GetCursorScreenPos();
float centerX = cursorPos.x + (availWidth - textSize.x) * 0.5f;
drawList->AddText(font, scaledFontSize, ImVec2(centerX, cursorPos.y), color, text);
ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + textSize.y + ImGui::GetStyle().ItemSpacing.y));
}


ImVec2 RenderGrayShadowCard_DrawList(const ImVec2& cardSize, float padding = 8.0f)
{
ImDrawList* drawList = ImGui::GetWindowDrawList();
const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
const float shadowOffset = 3.0f;
const float rounding = 6.0f;
drawList->AddRectFilled(ImVec2(cursorPos.x + shadowOffset, cursorPos.y + shadowOffset),ImVec2(cursorPos.x + cardSize.x + shadowOffset, cursorPos.y + cardSize.y + shadowOffset),IM_COL32(20, 20, 20, 80), rounding);
drawList->AddRectFilled(cursorPos,ImVec2(cursorPos.x + cardSize.x, cursorPos.y + cardSize.y),IM_COL32(230, 230, 230, 255), rounding);
drawList->AddRect(cursorPos,ImVec2(cursorPos.x + cardSize.x, cursorPos.y + cardSize.y),IM_COL32(180, 180, 180, 255), rounding);
const ImVec2 contentPos = ImVec2(cursorPos.x + padding, cursorPos.y + padding);
ImGui::SetCursorScreenPos(contentPos);
return ImVec2(cardSize.x - 2 * padding, cardSize.y - 2 * padding);
}



void SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f", 
ImGuiSliderFlags flags = 0, float cursor_x = 27.0f, float width = 0.0f, float overall_offset = 155.0f) {
ImGuiStyle& style = ImGui::GetStyle();  
ImVec4 originalGrab = style.Colors[ImGuiCol_SliderGrab];
ImVec4 originalGrabActive = style.Colors[ImGuiCol_SliderGrabActive];
float originalFrameHeight = style.FramePadding.y;   
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 12.0f);
ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 15));
ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.196f, 0.588f, 1.0f, 1.0f));

style.GrabMinSize = 0.0f;
float totalWidth = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
float sliderWidth = totalWidth * 0.4f;
ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cursor_x);
ImGui::SetNextItemWidth(sliderWidth);
ImGui::SliderFloat(("##" + std::string(label)).c_str(), v, v_min, v_max, "", flags);
ImGui::SameLine();
ImGui::SetCursorPosX(ImGui::GetCursorPosX() + overall_offset);
ImGui::Text("%s", label);

ImGui::SameLine();
ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
char valueText[32];
if (format) {
snprintf(valueText, sizeof(valueText), format, *v);
} else {
snprintf(valueText, sizeof(valueText), "%.2f", *v);
}
ImGui::Text("%s", valueText);
ImGui::PopStyleColor(6);
ImGui::PopStyleVar(3);
style.GrabMinSize = 30.0f;
style.FramePadding.y = originalFrameHeight;
ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
}

void TouchScroll(const bool& InertiaFlag = false) {
ImGuiWindow *WinPtr = ImGui::GetCurrentWindow();
float ScrollMaxY = ImGui::GetScrollMaxY();
if (WinPtr->SkipItems || ScrollMaxY <= 0.0f)
return;

static float ScrollVel = 0.0f;
static float ScrollAccel = 0.0f;
bool Hover = ImGui::IsWindowHovered();
ImGuiIO &GuiIO = ImGui::GetIO();

static ImVec2 StartTouchPos = ImVec2(0, 0);
static bool Scroll = false;
static float LastScrollY = 0.0f;
static double LastTime = 0.0;

auto UpdateScroll = [&](float deltaY, float deltaTime) {
float CurScrollY = ImGui::GetScrollY();
float NewScrollY = CurScrollY + deltaY;
NewScrollY = ImClamp(NewScrollY, 0.0f, ScrollMaxY);
ImGui::SetScrollY(NewScrollY);

ScrollVel = deltaY / (deltaTime + 0.0001f);
ScrollAccel = ScrollVel / (deltaTime + 0.0001f); // 计算加速度
LastScrollY = NewScrollY;
};

auto GetDeltaTime = [&]() {
double CurTime = ImGui::GetTime();
float DeltaTime = (float)(CurTime - LastTime);
LastTime = CurTime;
return DeltaTime;
};

auto ToggleParentWindowMove = [WinPtr](bool noMove) {
if (ImGuiWindow *ParWinPtr = WinPtr->ParentWindow) {
if (noMove) {
ParWinPtr->Flags |= ImGuiWindowFlags_NoMove;
} else {
ParWinPtr->Flags &= ~ImGuiWindowFlags_NoMove;
}
}
};
if (Hover && GuiIO.MouseDown[0] && !Scroll) {
StartTouchPos = GuiIO.MousePos;
Scroll = true;
LastScrollY = ImGui::GetScrollY();
LastTime = ImGui::GetTime();
ScrollVel = 0.0f; // 重置速度
ScrollAccel = 0.0f; // 重置加速度
ToggleParentWindowMove(true);
}
if (Scroll) {
float DeltaTime = GetDeltaTime();
if (GuiIO.MouseDown[0]) {
float DeltaY = StartTouchPos.y - GuiIO.MousePos.y;
UpdateScroll(DeltaY, DeltaTime);
StartTouchPos = GuiIO.MousePos;
} else {
Scroll = false;
ToggleParentWindowMove(false);
}
}
if (!Scroll && fabsf(ScrollVel) > 10.0f && InertiaFlag) {
float DeltaTime = GetDeltaTime();
float CurScrollY = ImGui::GetScrollY();
float NewScrollY = CurScrollY + ScrollVel * DeltaTime;
float damping = 0.95f;
ScrollVel *= powf(damping, DeltaTime * 60.0f);
if (NewScrollY <= 0.0f) {
NewScrollY = 0.0f;
ScrollVel = 0.0f;
} else if (NewScrollY >= ScrollMaxY)
{
NewScrollY = ScrollMaxY;
ScrollVel = 0.0f;
}
ImGui::SetScrollY(NewScrollY);
if (fabsf(ScrollVel) < 1.0f)
{
ScrollVel = 0.0f;
}
} else if (!Scroll)
{
ScrollVel = 0.0f;
}
}

#endif