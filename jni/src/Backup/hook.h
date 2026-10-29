// hook.h - ORIGINAL STRUCTURE WITH 14 BONES AIMBOT + SKELETON ESP + 360 ALERT

#include "bool.h"
#include <map>
#include <string>
#include <thread>
#include <cctype>

// ==================== 360 ESP ALERT VARIABLES ====================
bool is3600 = false;

// ==================== EXISTING CODE ====================
uintptr_t GetPlayerHeadTF(uintptr_t player) {
    auto ptr = Read<uintptr_t>(player + 0x648);
    return ptr;
}

uintptr_t GetPlayerPeTF(uintptr_t player) {
    auto ptr = Read<uintptr_t>(player + 0x670);
    return ptr;
}

uintptr_t GetPlayerMainCamera(uintptr_t player) {
    auto ptr = Read<uintptr_t>(player + 0x398);
    return ptr;
}

struct Matrix {
    Vector4 Position;
    Quaternion Rotation;
    Vector4 Scale;
};

static auto GetPosition(uintptr_t Transform) {
    auto pos = Vector3::Zero();

    auto transformObjValue = Read<uintptr_t>(Transform + 0x10);
    if (transformObjValue == 0) {
        return pos;
    }

    auto indexValue = Read<uintptr_t>(transformObjValue + 0x40);
    auto matrixValue = Read<uintptr_t>(transformObjValue + 0x38);

    auto matrixListValue = Read<uintptr_t>(matrixValue + 0x18);
    auto matrixIndicesValue = Read<uintptr_t>(matrixValue + 0x20);

    auto resultValue = Read<Vector3>(matrixListValue + (indexValue * 0x30));

    auto maxTries = 50;
    auto tries = 0;

    auto transformIndexValue = Read<int>(matrixIndicesValue + (indexValue * 4));

    while (transformIndexValue >= 0 && tries < maxTries) {
        tries++;

        auto tMatrixValue = Read<Matrix>(matrixListValue + (transformIndexValue * 0x30));

        auto rotX = tMatrixValue.Rotation.X;
        auto rotY = tMatrixValue.Rotation.Y;
        auto rotZ = tMatrixValue.Rotation.Z;
        auto rotW = tMatrixValue.Rotation.W;

        auto scaleX = resultValue.X * tMatrixValue.Scale.X;
        auto scaleY = resultValue.Y * tMatrixValue.Scale.Y;
        auto scaleZ = resultValue.Z * tMatrixValue.Scale.Z;

        resultValue.X = tMatrixValue.Position.X + scaleX +
                        (scaleX * ((rotY * rotY * -2.0) - (rotZ * rotZ * 2.0))) +
                        (scaleY * ((rotW * rotZ * -2.0) - (rotY * rotX * -2.0))) +
                        (scaleZ * ((rotZ * rotX * 2.0) - (rotW * rotY * -2.0)));
        resultValue.Y = tMatrixValue.Position.Y + scaleY +
                        (scaleX * ((rotX * rotY * 2.0) - (rotW * rotZ * -2.0))) +
                        (scaleY * ((rotZ * rotZ * -2.0) - (rotX * rotX * 2.0))) +
                        (scaleZ * ((rotW * rotX * -2.0) - (rotZ * rotY * -2.0)));
        resultValue.Z = tMatrixValue.Position.Z + scaleZ +
                        (scaleX * ((rotW * rotY * -2.0) - (rotX * rotZ * -2.0))) +
                        (scaleY * ((rotY * rotZ * 2.0) - (rotW * rotX * -2.0))) +
                        (scaleZ * ((rotX * rotX * -2.0) - (rotY * rotY * 2.0)));

        transformIndexValue = Read<int>(matrixIndicesValue + (transformIndexValue * 4));
    }

    if (tries < maxTries) {
        pos = resultValue;
    }

    return pos;
}

static auto GetNodePosition(uintptr_t nodeTransform) {
    auto transformValue = Read<uintptr_t>(nodeTransform + 0x10);
    if (transformValue == 0) {
        return Vector3::Zero();
    }
    return GetPosition(transformValue);
}

Vector3 GetBonePosition(uintptr_t player, uintptr_t offset) {
    uintptr_t bone = Read<uintptr_t>(player + offset);
    if (!bone) return Vector3::Zero();
    return GetNodePosition(bone);
}

// ==================== 360 ESP ALERT FUNCTIONS ====================
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float _x, float _y) : x(_x), y(_y) {}
};

bool isOutsideScreen(const Vec2& pos, const Vec2& screen) {
    if (pos.y < 0) return true;
    if (pos.x > screen.x) return true;
    if (pos.y > screen.y) return true;
    return pos.x < 0;
}

Vec2 pushToScreenBorder(const Vec2& Pos, const Vec2& screen, float offset) {
    float x = Pos.x, y = Pos.y;
    if (Pos.y < 0) y = -offset;
    if (Pos.x > screen.x) x = screen.x + offset;
    if (Pos.y > screen.y) y = screen.y + offset;
    if (Pos.x < 0) x = -offset;
    return Vec2(x, y);
}

void Draw360Alert(ImDrawList* draw, Vector3 headPos, float distance, float sw, float sh, bool isKnocked) {
    if (!is3600) return;
    if (headPos.X < 0 || headPos.Y < 0) return;
    
    Vec2 screen(sw, sh);
    Vec2 headPos2D(headPos.X, headPos.Y);
    
    if (isOutsideScreen(headPos2D, screen)) {
        Vec2 hintDotRenderPos = pushToScreenBorder(headPos2D, screen, -50);
        float radius = 35.0f;
        ImU32 circleColor = isKnocked ? IM_COL32(255, 100, 0, 200) : IM_COL32(255, 0, 0, 200);
        
        for (int i = 1; i <= 3; i++) {
            float alpha = 0.15f / i;
            draw->AddCircleFilled(ImVec2(hintDotRenderPos.x, hintDotRenderPos.y), radius + i, IM_COL32(255, 0, 0, (int)(alpha * 255)));
        }
        draw->AddCircleFilled(ImVec2(hintDotRenderPos.x, hintDotRenderPos.y), radius, circleColor);
        draw->AddCircle(ImVec2(hintDotRenderPos.x, hintDotRenderPos.y), radius, IM_COL32(255, 255, 255, 255), 0, 2.5f);
        
        std::string strDistance = "[" + std::to_string((int)distance) + "M]";
        ImVec2 textSize = ImGui::CalcTextSize(strDistance.c_str());
        draw->AddText(NULL, 18.0f, 
            ImVec2(hintDotRenderPos.x - textSize.x / 2, hintDotRenderPos.y - textSize.y / 2),
            IM_COL32(255, 255, 255, 255), strDistance.c_str());
        
        float angle = atan2(headPos2D.y - sh/2, headPos2D.x - sw/2);
        float arrowX = hintDotRenderPos.x + cos(angle) * (radius + 10);
        float arrowY = hintDotRenderPos.y + sin(angle) * (radius + 10);
        draw->AddTriangleFilled(
            ImVec2(arrowX, arrowY),
            ImVec2(arrowX - 8, arrowY - 5),
            ImVec2(arrowX - 8, arrowY + 5),
            IM_COL32(255, 255, 255, 255)
        );
    }
}

// ==================== 14 BONES AIMBOT ====================
int selectedBoneIndex = 0; 
const char* boneItems[] = { 
    "HEAD", "HIP", "CHEST", "ROOT",
    "LEFT SHOULDER", "RIGHT SHOULDER",
    "LEFT ANKLE", "RIGHT ANKLE",
    "LEFT TOE", "RIGHT TOE",
    "RIGHT FOREARM", "LEFT FOREARM",
    "RIGHT HAND", "LEFT HAND"
};

Vector3 GetTargetPosition(uintptr_t enemy) {
    uintptr_t boneBase = 0;
    Vector3 offset(0, 0, 0);

    switch(selectedBoneIndex) {
        case 0: boneBase = Read<uintptr_t>(enemy + 0x648); break;  // HEAD
        case 1: boneBase = Read<uintptr_t>(enemy + 0x650); offset = Vector3(0, 0.1f, 0); break;  // HIP
        case 2: boneBase = Read<uintptr_t>(enemy + 0x658); offset = Vector3(0, 0.1f, 0); break;  // CHEST
        case 3: boneBase = Read<uintptr_t>(enemy + 0x670); offset = Vector3(0, -0.2f, 0); break;  // ROOT
        case 4: boneBase = Read<uintptr_t>(enemy + 0x6B0); break;  // LEFT SHOULDER
        case 5: boneBase = Read<uintptr_t>(enemy + 0x6B8); break;  // RIGHT SHOULDER
        case 6: boneBase = Read<uintptr_t>(enemy + 0x680); offset = Vector3(0, 0.1f, 0); break;  // LEFT ANKLE
        case 7: boneBase = Read<uintptr_t>(enemy + 0x688); offset = Vector3(0, 0.1f, 0); break;  // RIGHT ANKLE
        case 8: boneBase = Read<uintptr_t>(enemy + 0x690); offset = Vector3(0, 0.05f, 0); break;  // LEFT TOE
        case 9: boneBase = Read<uintptr_t>(enemy + 0x698); offset = Vector3(0, 0.05f, 0); break;  // RIGHT TOE
        case 10: boneBase = Read<uintptr_t>(enemy + 0x6D0); break;  // RIGHT FOREARM
        case 11: boneBase = Read<uintptr_t>(enemy + 0x6D8); break;  // LEFT FOREARM
        case 12: boneBase = Read<uintptr_t>(enemy + 0x6C0); break;  // RIGHT HAND
        case 13: boneBase = Read<uintptr_t>(enemy + 0x6C8); break;  // LEFT HAND
        default: boneBase = Read<uintptr_t>(enemy + 0x648); break;
    }

    Vector3 bonePos = GetNodePosition(boneBase);
    return bonePos + offset;
}

static auto WorldToScreenPoint(D3DMatrix viewMatrix, Vector3 ScreenPos) {
    auto result = Vector3(-1, -1, -1);

    auto v9 = (ScreenPos.X * viewMatrix._11) + (ScreenPos.Y * viewMatrix._21) + (ScreenPos.Z * viewMatrix._31) + viewMatrix._41;
    auto v10 = (ScreenPos.X * viewMatrix._12) + (ScreenPos.Y * viewMatrix._22) + (ScreenPos.Z * viewMatrix._32) + viewMatrix._42;
    auto v12 = (ScreenPos.X * viewMatrix._14) + (ScreenPos.Y * viewMatrix._24) + (ScreenPos.Z * viewMatrix._34) + viewMatrix._44;

    if (v12 >= 0.001f) {
        auto v13 = (float)abs_ScreenX / 2.0f;
        auto v14 = (float)abs_ScreenY / 2.0f;

        result.X = v13 + (v13 * v9) / v12;
        result.Y = v14 - (v14 * v10) / v12;
    }
    return result;
}

// ==================== VARIABLES ====================
bool Enable = true;
bool showLine = false;
bool showDistance = false;
bool showBox = false;
bool showName = false;
bool showHealth = false;
bool showCount = false;
bool AimLock = false;
bool Aimbot = false;
bool AimSilent = false;
bool Ignorknock = false;
bool showAimLine = false;
bool showSkeleton = false;
float AimFov = 0.0f;
int textSize = 15.0f;

// Aim Visibility Check
bool aimVisibilityCheck = true;

// ===== BOX STYLE SETTINGS =====
int boxStyle = 1;
float boxThickness = 2.0f;
float boxCornerRound = 4.0f;
float boxGlowIntensity = 0.7f;
float boxFillAlpha = 0.3f;
int boxColorMode = 0;
ImVec4 boxCustomColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 boxGlowColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

// ===== LINE SETTINGS =====
int lineStyle = 0;
float lineThickness = 2.0f;
float lineGlowIntensity = 0.5f;
int lineColorMode = 0;
ImVec4 lineCustomColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

// ===== HEALTH BAR SETTINGS =====
int healthBarStyle = 0;
float healthBarWidth = 6.0f;
float healthBarRounding = 2.0f;
bool showHealthText = true;
bool showHealthColorGradient = true;
bool animateHealthBar = false;

// ===== EXTRA ESP FEATURES =====
bool showHeadDot = false;
float showHeadDotRadius = 5.0f;
ImVec4 headDotColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

char PlayerName[64];

float GetFOVRadius(float screenHeight) {
    return (AimFov / 360.0f) * (screenHeight / 2.0f);
}

std::mutex silentLock;
uintptr_t g_HitObjInfo = 0;
Vector3 g_TargetDir = {0, 0, 0};
bool g_HasData = false;
bool track = true;

void SilentAimThread() {
    while (track) {
        if (!g_HasData || g_HitObjInfo == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(silentLock);
            Write<Vector3>(g_HitObjInfo + 0x40, g_TargetDir); 
        }
    }
}

static std::chrono::steady_clock::time_point fireStartTime;
static bool isAlreadyFiring = false;

// ===== FOV CIRCLE - WHITE =====
void DrawFOVCircle(ImDrawList* draw, ImVec2 center, float radius) {
    constexpr int NUM_SEGMENTS = 100;
    constexpr float PI = 3.14159265f;
    const float thickness = 2.5f;
    ImU32 color = IM_COL32(255, 255, 255, 200);

    for (int i = 0; i < NUM_SEGMENTS; ++i) {
        float angle1 = (i * 2.0f * PI) / NUM_SEGMENTS;
        float angle2 = ((i + 1) * 2.0f * PI) / NUM_SEGMENTS;
        ImVec2 p1(center.x + cosf(angle1) * radius, center.y + sinf(angle1) * radius);
        ImVec2 p2(center.x + cosf(angle2) * radius, center.y + sinf(angle2) * radius);
        draw->AddLine(p1, p2, color, thickness);
    }
}

ImU32 GetHealthColor(int health) {
    if (health >= 80) return IM_COL32(0, 255, 0, 255);
    if (health >= 50) return IM_COL32(255, 255, 0, 255);
    if (health >= 25) return IM_COL32(255, 165, 0, 255);
    return IM_COL32(255, 0, 0, 255);
}

ImU32 GetDistanceColor(float distance) {
    if (distance < 50) return IM_COL32(255, 0, 0, 255);
    if (distance < 150) return IM_COL32(255, 255, 0, 255);
    return IM_COL32(0, 255, 0, 255);
}

ImU32 GetRainbowColor(float offset = 0.0f) {
    static float hue = 0.0f;
    hue += 0.005f;
    if (hue > 1.0f) hue -= 1.0f;
    ImColor color = ImColor::HSV(fmodf(hue + offset, 1.0f), 1.0f, 1.0f);
    return color;
}

ImU32 GetBoxColor(int health, float distance, bool isKnocked) {
    if (isKnocked) return IM_COL32(255, 100, 0, 255);
    
    switch (boxColorMode) {
        case 0: return ImGui::ColorConvertFloat4ToU32(boxCustomColor);
        case 1: return GetHealthColor(health);
        case 2: return GetDistanceColor(distance);
        case 3: return GetRainbowColor(0.0f);
        case 4: return IM_COL32(0, 255, 255, 255);
        default: return IM_COL32(255, 0, 0, 255);
    }
}

ImU32 GetLineColor(int health, float distance, bool isKnocked) {
    if (isKnocked) return IM_COL32(255, 100, 0, 255);
    
    switch (lineColorMode) {
        case 0: return ImGui::ColorConvertFloat4ToU32(lineCustomColor);
        case 1: return GetHealthColor(health);
        case 2: return GetDistanceColor(distance);
        case 3: return GetRainbowColor(0.3f);
        case 4: return IM_COL32(255, 255, 0, 255);
        default: return IM_COL32(0, 255, 0, 255);
    }
}

void DrawCornerBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color, float thickness) {
    float cornerLen = width * 0.25f;
    draw->AddLine(ImVec2(x, y), ImVec2(x + cornerLen, y), color, thickness);
    draw->AddLine(ImVec2(x, y), ImVec2(x, y + cornerLen), color, thickness);
    draw->AddLine(ImVec2(x + width - cornerLen, y), ImVec2(x + width, y), color, thickness);
    draw->AddLine(ImVec2(x + width, y), ImVec2(x + width, y + cornerLen), color, thickness);
    draw->AddLine(ImVec2(x, y + height), ImVec2(x + cornerLen, y + height), color, thickness);
    draw->AddLine(ImVec2(x, y + height - cornerLen), ImVec2(x, y + height), color, thickness);
    draw->AddLine(ImVec2(x + width - cornerLen, y + height), ImVec2(x + width, y + height), color, thickness);
    draw->AddLine(ImVec2(x + width, y + height - cornerLen), ImVec2(x + width, y + height), color, thickness);
}

void DrawGlowBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color, float intensity) {
    for (int i = 1; i <= 4; i++) {
        float alpha = (intensity * 0.2f) * (1.0f - (i / 5.0f));
        ImU32 glowColor = (color & 0x00FFFFFF) | ((int)(alpha * 255) << 24);
        draw->AddRect(ImVec2(x - i, y - i), ImVec2(x + width + i, y + height + i), glowColor, 0.0f, 0, 1.0f);
    }
    draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), color, 0.0f, 0, 2.0f);
}

void DrawDashedBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color, float thickness) {
    float dashLen = 8.0f;
    for (float tx = x; tx < x + width; tx += dashLen * 2) {
        draw->AddLine(ImVec2(tx, y), ImVec2(ImMin(tx + dashLen, x + width), y), color, thickness);
        draw->AddLine(ImVec2(tx, y + height), ImVec2(ImMin(tx + dashLen, x + width), y + height), color, thickness);
    }
    for (float ty = y; ty < y + height; ty += dashLen * 2) {
        draw->AddLine(ImVec2(x, ty), ImVec2(x, ImMin(ty + dashLen, y + height)), color, thickness);
        draw->AddLine(ImVec2(x + width, ty), ImVec2(x + width, ImMin(ty + dashLen, y + height)), color, thickness);
    }
}

void DrawDoubleBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color, float thickness) {
    draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), color, 0.0f, 0, thickness);
    draw->AddRect(ImVec2(x + 3, y + 3), ImVec2(x + width - 3, y + height - 3), color, 0.0f, 0, thickness * 0.7f);
}

void Draw3DBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color) {
    ImU32 darkColor = (color & 0x00FFFFFF) | (int)((color >> 24) * 0.5f) << 24;
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), color);
    draw->AddRectFilled(ImVec2(x + 2, y + 2), ImVec2(x + width - 2, y + height - 2), darkColor);
    draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.0f);
}

void DrawAdvancedBox(ImDrawList* draw, float x, float y, float width, float height, ImU32 color, int health, float distance, bool isKnocked) {
    ImU32 finalColor = GetBoxColor(health, distance, isKnocked);
    
    switch (boxStyle) {
        case 0: draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), finalColor, 0.0f, 0, boxThickness); break;
        case 1: draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), (finalColor & 0x00FFFFFF) | ((int)(boxFillAlpha * 255) << 24));
                draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), finalColor, 0.0f, 0, boxThickness); break;
        case 2: draw->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + width + 1, y + height + 1), IM_COL32(0, 0, 0, 200), 0.0f, 0, boxThickness + 1);
                draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), finalColor, 0.0f, 0, boxThickness); break;
        case 3: draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), (finalColor & 0x00FFFFFF) | ((int)(0.2f * 255) << 24));
                draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), finalColor, 0.0f, 0, boxThickness); break;
        case 4: DrawCornerBox(draw, x, y, width, height, finalColor, boxThickness); break;
        case 5: DrawGlowBox(draw, x, y, width, height, finalColor, boxGlowIntensity); break;
        case 6: DrawDashedBox(draw, x, y, width, height, finalColor, boxThickness); break;
        case 7: DrawDoubleBox(draw, x, y, width, height, finalColor, boxThickness); break;
        case 8: draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), finalColor, boxCornerRound, 0, boxThickness); break;
        case 9: Draw3DBox(draw, x, y, width, height, finalColor); break;
    }
}

void DrawAdvancedHealthBar(ImDrawList* draw, float x, float y, float width, float height, int health) {
    float healthPercent = ImClamp(health / 100.0f, 0.0f, 1.0f);
    float fillHeight = height * healthPercent;
    float fillY = y + (height - fillHeight);
    
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(0, 0, 0, 150), healthBarRounding);
    
    ImU32 healthColor = showHealthColorGradient ? GetHealthColor(health) : IM_COL32(0, 255, 0, 255);
    
    switch (healthBarStyle) {
        case 0: draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, healthBarRounding); break;
        case 1: draw->AddRectFilledMultiColor(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, healthColor, IM_COL32(255, 0, 0, 255), IM_COL32(255, 0, 0, 255)); break;
        case 2: for (float i = 0; i < width; i++) { float t = i / width; draw->AddLine(ImVec2(x + i, fillY), ImVec2(x + i, y + height), IM_COL32(255 * (1 - t), 255 * t, 0, 255), 1.0f); } break;
        case 3: draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, 1.0f); break;
        case 4: draw->AddRectFilled(ImVec2(x - 1, fillY - 1), ImVec2(x + width + 1, y + height + 1), healthColor, healthBarRounding); break;
        case 5: draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, healthBarRounding);
                draw->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + width + 1, y + height + 1), IM_COL32(255, 255, 255, 200), healthBarRounding, 0, 1.0f); break;
        case 6: draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, healthBarRounding); break;
        case 7: for (float i = 0; i < height; i++) { draw->AddLine(ImVec2(x, y + i), ImVec2(x + width, y + i), GetRainbowColor(i / height), 1.0f); } break;
        case 8: if (animateHealthBar) { static float pulse = 0.0f; pulse += 0.05f; float alpha = 0.5f + sinf(pulse) * 0.5f;
                draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), (healthColor & 0x00FFFFFF) | ((int)(alpha * 255) << 24), healthBarRounding); }
                else { draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, healthBarRounding); } break;
        case 9: draw->AddRectFilled(ImVec2(x, fillY), ImVec2(x + width, y + height), healthColor, 0.0f);
                for (float yy = fillY; yy < y + height; yy += 3) { draw->AddLine(ImVec2(x, yy), ImVec2(x + width, yy), IM_COL32(0, 0, 0, 100), 1.0f); } break;
    }
    
    if (showHealthText && health > 0) {
        char healthText[16];
        sprintf(healthText, "%d", health);
        ImVec2 textSizeCalc = ImGui::GetFont()->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, healthText);
        draw->AddText(NULL, 12.0f, ImVec2(x - textSizeCalc.x - 2, y + (height / 2) - 6), IM_COL32(255, 255, 255, 255), healthText);
    }
}

void DrawAdvancedLine(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float distance, int health, bool isKnocked) {
    ImU32 finalColor = GetLineColor(health, distance, isKnocked);
    
    switch (lineStyle) {
        case 0: draw->AddLine(from, to, finalColor, lineThickness); break;
        case 1: { float length = sqrt(pow(to.x - from.x, 2) + pow(to.y - from.y, 2));
                  for (float t = 0; t < length; t += 8.0f) {
                      float tx = from.x + (to.x - from.x) * (t / length);
                      float ty = from.y + (to.y - from.y) * (t / length);
                      draw->AddCircleFilled(ImVec2(tx, ty), 2.0f, finalColor);
                  } } break;
        case 2: { float dashLen = 15.0f; float totalLen = sqrt(pow(to.x - from.x, 2) + pow(to.y - from.y, 2));
                  for (int i = 0; i < (int)(totalLen / dashLen); i++) {
                      float t1 = i * dashLen / totalLen;
                      float t2 = (i * dashLen + dashLen / 2) / totalLen;
                      if (t2 > 1.0f) t2 = 1.0f;
                      draw->AddLine(ImVec2(from.x + (to.x - from.x) * t1, from.y + (to.y - from.y) * t1),
                                    ImVec2(from.x + (to.x - from.x) * t2, from.y + (to.y - from.y) * t2), finalColor, lineThickness);
                  } } break;
        case 3: draw->AddLine(from, to, finalColor, lineThickness * 2); break;
        case 4: for (float t = 0; t <= 1.0f; t += 0.05f) {
                  draw->AddCircleFilled(ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t), lineThickness, GetRainbowColor(t));
                } break;
        case 5: for (int i = 1; i <= 3; i++) {
                  float alpha = (lineGlowIntensity * 0.3f) * (1.0f - (i / 4.0f));
                  ImU32 glowColor = (finalColor & 0x00FFFFFF) | ((int)(alpha * 255) << 24);
                  draw->AddLine(ImVec2(from.x - i, from.y - i), ImVec2(to.x - i, to.y - i), glowColor, lineThickness);
                  draw->AddLine(ImVec2(from.x + i, from.y + i), ImVec2(to.x + i, to.y + i), glowColor, lineThickness);
                } draw->AddLine(from, to, finalColor, lineThickness); break;
        case 6: draw->AddLine(from, to, finalColor, lineThickness);
                { ImVec2 dir = ImVec2(to.x - from.x, to.y - from.y);
                  float len = sqrt(dir.x * dir.x + dir.y * dir.y);
                  if (len > 0) { dir.x /= len; dir.y /= len;
                    ImVec2 perp = ImVec2(-dir.y, dir.x);
                    ImVec2 arrowBase = ImVec2(to.x - dir.x * 15, to.y - dir.y * 15);
                    draw->AddLine(to, ImVec2(arrowBase.x + perp.x * 8, arrowBase.y + perp.y * 8), finalColor, lineThickness);
                    draw->AddLine(to, ImVec2(arrowBase.x - perp.x * 8, arrowBase.y - perp.y * 8), finalColor, lineThickness);
                  } } break;
        case 7: draw->AddLine(from, to, finalColor, lineThickness);
                draw->AddLine(ImVec2(from.x + 2, from.y + 2), ImVec2(to.x + 2, to.y + 2), finalColor, lineThickness * 0.5f); break;
        case 8: draw->AddLine(from, to, finalColor, lineThickness);
                draw->AddLine(ImVec2(from.x - 1, from.y - 1), ImVec2(to.x - 1, to.y - 1), IM_COL32(0, 255, 255, 100), lineThickness);
                draw->AddLine(ImVec2(from.x + 1, from.y + 1), ImVec2(to.x + 1, to.y + 1), IM_COL32(255, 0, 255, 100), lineThickness); break;
        case 9: draw->AddLine(from, to, finalColor, 1.0f);
                draw->AddLine(ImVec2(from.x, from.y + 1), ImVec2(to.x, to.y + 1), IM_COL32(255, 0, 0, 200), 1.0f); break;
    }
}

// ==================== SKELETON ESP ====================
void DrawSkeletonESP(ImDrawList* draw, uintptr_t enemy, D3DMatrix matrix) {
    if (!showSkeleton) return;
    
    // Get all bone positions
    Vector3 head = GetBonePosition(enemy, 0x648);
    Vector3 chest = GetBonePosition(enemy, 0x658);
    Vector3 hip = GetBonePosition(enemy, 0x650);
    Vector3 leftShoulder = GetBonePosition(enemy, 0x6B0);
    Vector3 rightShoulder = GetBonePosition(enemy, 0x6B8);
    Vector3 leftHand = GetBonePosition(enemy, 0x6C8);
    Vector3 rightHand = GetBonePosition(enemy, 0x6C0);
    Vector3 leftAnkle = GetBonePosition(enemy, 0x680);
    Vector3 rightAnkle = GetBonePosition(enemy, 0x688);
    
    // Convert to screen
    Vector3 sHead = WorldToScreenPoint(matrix, head);
    Vector3 sChest = WorldToScreenPoint(matrix, chest);
    Vector3 sHip = WorldToScreenPoint(matrix, hip);
    Vector3 sLeftShoulder = WorldToScreenPoint(matrix, leftShoulder);
    Vector3 sRightShoulder = WorldToScreenPoint(matrix, rightShoulder);
    Vector3 sLeftHand = WorldToScreenPoint(matrix, leftHand);
    Vector3 sRightHand = WorldToScreenPoint(matrix, rightHand);
    Vector3 sLeftAnkle = WorldToScreenPoint(matrix, leftAnkle);
    Vector3 sRightAnkle = WorldToScreenPoint(matrix, rightAnkle);
    
    ImU32 skColor = IM_COL32(100, 200, 255, 220);
    float thick = 1.8f;
    
    // Spine
    if (sHead.X > 0 && sChest.X > 0) draw->AddLine(ImVec2(sHead.X, sHead.Y), ImVec2(sChest.X, sChest.Y), skColor, thick);
    if (sChest.X > 0 && sHip.X > 0) draw->AddLine(ImVec2(sChest.X, sChest.Y), ImVec2(sHip.X, sHip.Y), skColor, thick);
    
    // Arms
    if (sChest.X > 0 && sLeftShoulder.X > 0) draw->AddLine(ImVec2(sChest.X, sChest.Y), ImVec2(sLeftShoulder.X, sLeftShoulder.Y), skColor, thick);
    if (sChest.X > 0 && sRightShoulder.X > 0) draw->AddLine(ImVec2(sChest.X, sChest.Y), ImVec2(sRightShoulder.X, sRightShoulder.Y), skColor, thick);
    if (sLeftShoulder.X > 0 && sLeftHand.X > 0) draw->AddLine(ImVec2(sLeftShoulder.X, sLeftShoulder.Y), ImVec2(sLeftHand.X, sLeftHand.Y), skColor, thick);
    if (sRightShoulder.X > 0 && sRightHand.X > 0) draw->AddLine(ImVec2(sRightShoulder.X, sRightShoulder.Y), ImVec2(sRightHand.X, sRightHand.Y), skColor, thick);
    
    // Legs
    if (sHip.X > 0 && sLeftAnkle.X > 0) draw->AddLine(ImVec2(sHip.X, sHip.Y), ImVec2(sLeftAnkle.X, sLeftAnkle.Y), skColor, thick);
    if (sHip.X > 0 && sRightAnkle.X > 0) draw->AddLine(ImVec2(sHip.X, sHip.Y), ImVec2(sRightAnkle.X, sRightAnkle.Y), skColor, thick);
}

// ==================== AIMBOT MENU ====================
void AimbotMenu(float sw, float sh, uintptr_t libAddress) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!Enable) return;
    
    uintptr_t gameFacadeOffset = 0xA984D48; 
    auto GameFacade_c = Read<uintptr_t>(libAddress + gameFacadeOffset);
    if (!GameFacade_c) return;

    auto P2 = Read<uintptr_t>(GameFacade_c + 0xB8);
    auto BaseGame = Read<uintptr_t>(P2);
    auto m_Match = Read<uintptr_t>(BaseGame + 0x90);
    if (!m_Match) return;

    int MatchIsRunning = Read<int>(m_Match + 0xcc);
    if (MatchIsRunning != 1) return;

    auto localPlayer = Read<uintptr_t>(m_Match + 0xd8);
    if (!localPlayer) return;

    auto FollowCamera = Read<uintptr_t>(localPlayer + 0x638);
    auto Camera = Read<uintptr_t>(FollowCamera + 0x30);
    auto IntPtrCam = Read<uintptr_t>(Camera + 0x10);
    if (!IntPtrCam) return;

    auto matrix = Read<D3DMatrix>(IntPtrCam + 0x100);
    auto dictionary = Read<uintptr_t>(BaseGame + 0xC0);
    if (!dictionary) return;

    auto entitylist = Read<uintptr_t>(dictionary + 0x18);
    if (!entitylist) return;
    
    Vector3 LocalPosition = GetNodePosition(GetPlayerHeadTF(localPlayer));
    
    uintptr_t bestTarget = 0;
    Vector3 bestPos;
    float bestDist = FLT_MAX;

    for (int i = 0; i < 1000; i++) {
        auto enemy = Read<uintptr_t>(entitylist + i*8);
        if (!enemy || enemy == localPlayer) continue;
        
        auto AvatarManager = Read<uintptr_t>(enemy + 0x718);
        if (!AvatarManager) continue;

        auto UmaAvatarSimple = Read<uintptr_t>(AvatarManager + 0x128);
        if (!UmaAvatarSimple) continue;

        if (!Read<bool>(UmaAvatarSimple + 0x101)) continue;

        auto UmaData = Read<uintptr_t>(UmaAvatarSimple + 0x28);
        if (!UmaData) continue;
        
        auto KHDMPGBLNCM = Read<uintptr_t>(enemy + 0x19f8);

        bool isKnocked = false;

        if (KHDMPGBLNCM != 0) {
            auto maybeDead = Read<uintptr_t>(KHDMPGBLNCM + 0x20);
            if (maybeDead && Read<int>(maybeDead + 0x10) == 8)
                isKnocked = true;
        }
        
        if (Ignorknock && isKnocked) continue;
        if (Read<bool>(UmaData + 0x81)) continue;
        if (Read<bool>(enemy + 0x7C)) continue;
        
        if (aimVisibilityCheck) {
            bool isVisible = Read<bool>(UmaAvatarSimple + 0x101);
            if (!isVisible) continue;
        }
        
        auto HedColider = Read<uintptr_t>(enemy + 0x6e0);
        if (AimLock) Write(enemy + 0x80, HedColider);
        
        Vector3 EnemyPos = GetTargetPosition(enemy);
        if (EnemyPos == Vector3::Zero()) continue;

        auto screen = WorldToScreenPoint(matrix, EnemyPos);

        float screenDist = sqrt(
            (screen.X - sw / 2) * (screen.X - sw / 2) +
            (screen.Y - sh / 2) * (screen.Y - sh / 2)
        );
        
        float distanceToEnemy = Vector3::Distance(LocalPosition, EnemyPos);
        
        // ===== 360 ESP ALERT =====
        if (is3600 && !isKnocked && screen.X > 0 && screen.Y > 0) {
            Draw360Alert(draw, screen, distanceToEnemy, sw, sh, isKnocked);
        }

        if (screenDist < AimFov && screenDist < bestDist) {
            bestDist = screenDist;
            bestTarget = enemy;
            bestPos = EnemyPos;
        }
    }

    if (bestTarget && showAimLine) {
        Vector3 bestScreen = WorldToScreenPoint(matrix, bestPos);
        if (bestScreen.X > 0 && bestScreen.Y > 0) {
            draw->AddLine(ImVec2(sw / 2.0f, sh / 2.0f), ImVec2(bestScreen.X, bestScreen.Y), IM_COL32(255, 0, 0, 255), 2.5f);
        }
    }

    if (bestTarget && AimSilent && track) {
        uintptr_t hitObjAddress = Read<uintptr_t>(localPlayer + 0xd80);
        int isFiring = Read<int>(localPlayer + 0x7e0);
    
        if (isFiring > 0 && hitObjAddress != 0) {
            Vector3 ammoBase = Read<Vector3>(hitObjAddress + 0x4c);
        
            Vector3 dir;
            dir.X = bestPos.X - ammoBase.X;
            dir.Y = bestPos.Y - ammoBase.Y;
            dir.Z = bestPos.Z - ammoBase.Z;

            silentLock.lock();
            g_HitObjInfo = hitObjAddress;
            g_TargetDir = dir;
            g_HasData = true;
            silentLock.unlock();
        }
    } else {
        if (g_HasData) {
            silentLock.lock();
            g_HasData = false;
            g_HitObjInfo = 0;
            silentLock.unlock();
        }
    }
    
    if (bestTarget && Aimbot) {
        auto CameraPosition = GetPosition(GetPlayerMainCamera(localPlayer));
        int isFiring = Read<int>(localPlayer + 0x7e0);

        if (isFiring > 0) {
            Quaternion rot = GetRotationToTheLocation(bestPos, 0.0f, CameraPosition);
            Write<Quaternion>(localPlayer + 0x5BC, rot);
        }
    }

    if (AimFov > 0) {
        float fovRadius = GetFOVRadius(sh);
        ImVec2 center(sw / 2.0f, sh / 2.0f);
        DrawFOVCircle(draw, center, fovRadius);
    }
}

// ==================== DRAW ESP FUNCTION ====================

void DrawESP(float sw, float sh, uintptr_t libAddress) {
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!Enable) return;
    
    uintptr_t gameFacadeOffset = 0xA984D48; 
    auto GameFacade_c = Read<uintptr_t>(libAddress + gameFacadeOffset);
    if (GameFacade_c) {
        auto P2 = Read<uintptr_t>(GameFacade_c + 0xB8);
        if (P2) {
            auto BaseGame = Read<uintptr_t>(P2);
            if (BaseGame) {
                auto m_Match = Read<uintptr_t>(BaseGame + 0x90);
                if (m_Match) {
                    int MatchIsRunning = Read<int>(m_Match + 0xcc);
                    if (MatchIsRunning != 4 && MatchIsRunning != 0 && MatchIsRunning == 1) {
                        auto localPlayer = Read<uintptr_t>(m_Match + 0xd8);
                        if (localPlayer != 0) {
                            auto FollowCamera = Read<uintptr_t>(localPlayer + 0x638);
                            if (FollowCamera != 0) {
                                auto Camera = Read<uintptr_t>(FollowCamera + 0x30);
                                if (Camera != 0) {
                                    auto IntPtrCam = Read<uintptr_t>(Camera + 0x10);
                                    if (IntPtrCam != 0) {
                                        auto matrix = Read<D3DMatrix>(IntPtrCam + 0x100);
                                        int playerCount = 0;
                                        int botCount = 0;
                                        auto dictionary = Read<uintptr_t>(BaseGame + 0xC0);
                                        if (dictionary) {
                                            auto entitylist = Read<uintptr_t>(dictionary + 0x18);
                                            Vector3 LocalPosition = GetNodePosition(GetPlayerHeadTF(localPlayer));
                                            
                                            for (int i = 0; i < 1000; i++) {
                                                auto enemy = Read<uintptr_t>(entitylist + i*8);
                                                if (enemy != 0) {
                                                    auto AvatarManager = Read<uintptr_t>(enemy + 0x718);
                                                    if (AvatarManager != 0 && enemy != localPlayer) {
                                                        auto UmaAvatarSimple = Read<uintptr_t>(AvatarManager + 0x128);
                                                        if (UmaAvatarSimple != 0) {
                                                            bool isVisible = Read<bool>(UmaAvatarSimple + 0x101);
                                                            
                                                            if (isVisible) {
                                                                auto UmaData = Read<uintptr_t>(UmaAvatarSimple + 0x28);
                                                                if (UmaData != 0) {
                                                                    auto KHDMPGBLNCM = Read<uintptr_t>(enemy + 0x19f8);
                                                                    if (KHDMPGBLNCM != 0) {
                                                                        bool isKnocked = false;
                                                                        auto maybeDead = Read<uintptr_t>(KHDMPGBLNCM + 0x20);
                                                                        if (maybeDead && Read<int>(maybeDead + 0x10) == 8) isKnocked = true;
                                                                        
                                                                        auto get_hp = ReadChain(enemy, {0x70, 0x10, 0x20});
                                                                        auto isClientBot = Read<int>(enemy + 0x450);
                                                                        memset(PlayerName, 0, 64);
                                                                        
                                                                        bool isBot = false;
                                                                        if (isClientBot == 1) {
                                                                            isBot = true;
                                                                            strcpy(PlayerName, "BOT");
                                                                            botCount++;
                                                                        } else {                                                                                
                                                                            uintptr_t NamePtr = Read<uintptr_t>(enemy + 0x448); 
                                                                            if (NamePtr > 0x10000000) {
                                                                                getUTF8(PlayerName, NamePtr);
                                                                            }
                                                                            if (PlayerName[0] == '\0') {
                                                                                strcpy(PlayerName, "BOT");
                                                                                isBot = true;
                                                                                botCount++;
                                                                            } else {
                                                                                char checkName[16];
                                                                                strncpy(checkName, PlayerName, 15);
                                                                                checkName[15] = '\0';
                                                                                bool onlyNumbers = true;
                                                                                for (int ci = 0; ci < (int)strlen(checkName) && ci < 15; ci++) {
                                                                                    if (!isdigit(checkName[ci]) && checkName[ci] != '\0') {
                                                                                        onlyNumbers = false;
                                                                                        break;
                                                                                    }
                                                                                }
                                                                                if (onlyNumbers || strstr(checkName, "Player") != nullptr || checkName[0] == '\0') {
                                                                                    strcpy(PlayerName, "BOT");
                                                                                    isBot = true;
                                                                                    botCount++;
                                                                                } else {
                                                                                    playerCount++;
                                                                                }
                                                                            }
                                                                        }
                                                                        
                                                                        auto Health = Read<int>(get_hp + 0x18);
                                                                                                                                  
                                                                        if (Read<bool>(UmaData + 0x81) != 0) continue;
                                                                        if (Read<bool>(enemy + 0x7c) != 0) continue;                                                            
                                                                        
                                                                        auto EnemyPosition = GetNodePosition(GetPlayerHeadTF(enemy));
                                                                        auto EnemyPositionPe = GetNodePosition(GetPlayerPeTF(enemy));
                                                                        auto CameraPosition = GetPosition(GetPlayerMainCamera(enemy));
                                                                        if (LocalPosition == Vector3::Zero() || EnemyPosition == Vector3::Zero() || CameraPosition == Vector3::Zero()) continue;
                                                                        
                                                                        float distanceToEnemy = Vector3::Distance(LocalPosition, EnemyPosition);
                                                                        auto screenPos = WorldToScreenPoint(matrix, EnemyPosition);
                                                                            
                                                                        auto LocationHeadBox = WorldToScreenPoint(matrix, EnemyPosition);
                                                                        auto LocationToeBox = WorldToScreenPoint(matrix, EnemyPositionPe);
                                                                        if (LocationHeadBox.X < -0 || LocationToeBox.X < -0) continue;
                                                                        
                                                                        float headToFeetHeight = abs(LocationToeBox.Y - LocationHeadBox.Y);
                                                                        float boxHeight = headToFeetHeight * 1.15f; 
                                                                        float width = boxHeight * 0.48f;
                                                                            
                                                                        float boxX = LocationHeadBox.X - (width / 2.0f);
                                                                        float boxY = LocationHeadBox.Y - (boxHeight * 0.12f);
                                                                        
                                                                        // ===== SKELETON ESP =====
                                                                        DrawSkeletonESP(draw, enemy, matrix);
                                                                        
                                                                        // ===== 360 ESP ALERT IN ESP =====
                                                                        if (is3600 && !isKnocked && screenPos.X > 0 && screenPos.Y > 0) {
                                                                            Draw360Alert(draw, screenPos, distanceToEnemy, sw, sh, isKnocked);
                                                                        }
                                                                            
                                                                        if (showBox) {
                                                                            DrawAdvancedBox(draw, boxX, boxY, width, boxHeight, 0, Health, distanceToEnemy, isKnocked);
                                                                        }
                                                                        
                                                                        if (showHealth && Health > 0 && Health <= 200) {
                                                                            DrawAdvancedHealthBar(draw, boxX - healthBarWidth - 2, boxY, healthBarWidth, boxHeight, Health);
                                                                        }
                                                                        
                                                                        if (showLine) {
                                                                            DrawAdvancedLine(draw, ImVec2(sw / 2.0f, 80.0f), ImVec2(screenPos.X, screenPos.Y), 0, distanceToEnemy, Health, isKnocked);
                                                                        }
                                                                        
                                                                        if (showHeadDot) {
                                                                            draw->AddCircleFilled(ImVec2(LocationHeadBox.X, LocationHeadBox.Y), showHeadDotRadius, ImGui::ColorConvertFloat4ToU32(headDotColor));
                                                                        }
                                                                        
                                                                        if (showDistance) {
                                                                            char distText[32];
                                                                            sprintf(distText, "%.0fm", distanceToEnemy);
                                                                            ImVec2 textSizeCalc = ImGui::GetFont()->CalcTextSizeA(textSize, FLT_MAX, 0.0f, distText);
                                                                            draw->AddText(NULL, textSize, ImVec2(boxX + (width/2) - (textSizeCalc.x/2), boxY - textSizeCalc.y - 2), IM_COL32(255, 255, 255, 255), distText);
                                                                        }
                                                                        
                                                                        if (showName) {
                                                                            ImVec2 textSizeCalc = ImGui::GetFont()->CalcTextSizeA(textSize, FLT_MAX, 0.0f, PlayerName);
                                                                            ImU32 nameColor = isBot ? IM_COL32(255, 165, 0, 255) : IM_COL32(0, 255, 0, 255);
                                                                            draw->AddText(NULL, textSize, ImVec2(boxX + (width/2) - (textSizeCalc.x/2), boxY - textSizeCalc.y - 2 - 18), nameColor, PlayerName);
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        
                                        // ===== ESP COUNT BOX =====
                                        if (showCount) {
                                            char countText[100];
                                            float centerX = sw / 2.0f;
                                            float startY = 45.0f;
                                            
                                            float boxWidth = 480.0f;
                                            float boxHeight = 85.0f;
                                            float boxX = centerX - (boxWidth / 2.0f);
                                            float boxY = startY - 15.0f;
                                            
                                            draw->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxWidth, boxY + boxHeight), IM_COL32(20, 20, 25, 240), 12.0f);
                                            
                                            for (int g = 1; g <= 3; g++) {
                                                float alpha = 0.15f / g;
                                                draw->AddRect(ImVec2(boxX - g, boxY - g), ImVec2(boxX + boxWidth + g, boxY + boxHeight + g), IM_COL32(255, 50, 50, (int)(alpha * 255)), 12.0f, 0, 1.5f);
                                            }
                                            draw->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxWidth, boxY + boxHeight), IM_COL32(255, 40, 40, 255), 12.0f, 0, 2.5f);
                                            draw->AddRect(ImVec2(boxX + 2, boxY + 2), ImVec2(boxX + boxWidth - 2, boxY + boxHeight - 2), IM_COL32(255, 80, 80, 100), 10.0f, 0, 1.0f);
                                            
                                            if (playerCount == 0 && botCount == 0) {
                                                sprintf(countText, "SAFE");
                                                ImVec2 safeSize = ImGui::GetFont()->CalcTextSizeA(42.0f, FLT_MAX, 0.0f, countText);
                                                for (int g = 1; g <= 2; g++) {
                                                    draw->AddText(NULL, 42.0f, ImVec2(centerX - (safeSize.x / 2.0f) - g, startY + 12.0f - g), IM_COL32(0, 200, 0, 80), countText);
                                                }
                                                draw->AddText(NULL, 42.0f, ImVec2(centerX - (safeSize.x / 2.0f), startY + 12.0f), IM_COL32(0, 255, 0, 255), countText);
                                            } else {
                                                float textSizeCount = 38.0f;
                                                float textY = startY + 18.0f;
                                                
                                                sprintf(countText, "PLAYER : %d", playerCount);
                                                float playerX = centerX - 180.0f;
                                                for (int g = 1; g <= 2; g++) {
                                                    draw->AddText(NULL, textSizeCount, ImVec2(playerX - g, textY - g), IM_COL32(255, 50, 50, 60), countText);
                                                    draw->AddText(NULL, textSizeCount, ImVec2(playerX + g, textY + g), IM_COL32(255, 50, 50, 60), countText);
                                                }
                                                draw->AddText(NULL, textSizeCount, ImVec2(playerX, textY), IM_COL32(255, 40, 40, 255), countText);
                                                
                                                sprintf(countText, "BOT : %d", botCount);
                                                float botX = centerX + 80.0f;
                                                for (int g = 1; g <= 2; g++) {
                                                    draw->AddText(NULL, textSizeCount, ImVec2(botX - g, textY - g), IM_COL32(200, 200, 200, 60), countText);
                                                }
                                                draw->AddText(NULL, textSizeCount, ImVec2(botX, textY), IM_COL32(255, 255, 255, 255), countText);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}