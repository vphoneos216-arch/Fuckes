#pragma once
#include "importz.h"
#include "aimbot.h"
#include "BatteryInfo.h"


#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>


void DrawESP(float screenWidth, float screenHeight) {
ImDrawList* draw = ImGui::GetBackgroundDrawList();
if (!draw) return;


if (Fov) {
ImVec2 screenSize = ImGui::GetIO().DisplaySize;
ImVec2 screenCenter = ImVec2(screenSize.x / 2, screenSize.y / 2); 
float radius = Fov_Aim;
ImU32 fovColor = ImColor(allCores);
ImGui::GetBackgroundDrawList()->AddCircle(screenCenter, radius, fovColor, 100, 2.0f);
}
if (Crosshair) {
ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);
float pointRadius = 3.0f;
draw->AddCircleFilled(center, pointRadius, ImColor(255, 255, 255, 255));
float lineLength = 15.0f;
float lineThickness = 2.0f;
ImColor lineColor(255, 255, 255, 255);
draw->AddLine(ImVec2(center.x - lineLength - pointRadius, center.y), 
ImVec2(center.x - pointRadius, center.y), 
lineColor, lineThickness);
draw->AddLine(ImVec2(center.x + pointRadius, center.y), 
ImVec2(center.x + lineLength + pointRadius, center.y), 
lineColor, lineThickness);
draw->AddLine(ImVec2(center.x, center.y - lineLength - pointRadius), 
ImVec2(center.x, center.y - pointRadius), 
lineColor, lineThickness);
draw->AddLine(ImVec2(center.x, center.y + pointRadius), 
ImVec2(center.x, center.y + lineLength + pointRadius), 
lineColor, lineThickness);
}
if (Hexagono) {
ImVec2 center(screenWidth / 2.0f, screenHeight / 2.0f);
float radius = 30.0f;
float angleOffset = PI / 2.0f;
ImVec2 vertices[6];
for (int i = 0; i < 6; ++i) {
float angle = angleOffset + i * (2 * PI / 6);
vertices[i] = ImVec2(center.x + cos(angle) * radius, center.y + sin(angle) * radius);
}
draw->AddPolyline(vertices, 6, ImColor(colorHexagono), true, 2.0f);
}

if (telegram) {
    float rectX = 10.0f;
    float rectY = screenHeight - 60.0f;

    // Obter data e hora atual
    time_t now = time(0);
    struct tm* timeInfo = localtime(&now);
    char dateTime[64];
    strftime(dateTime, sizeof(dateTime), "%d/%m/%Y - %H:%M:%S", timeInfo); // Ex: 29/07/2025 - 22:33:12

    // Obter porcentagem da bateria
    int battery = GetBatteryPercent();
    char batteryText[32];
    sprintf(batteryText, " | Bateria: %d%%", battery);

    // Combinar texto final (data + bateria)
    std::string fullText = std::string(dateTime) + std::string(batteryText);

    // Tamanho do texto completo
    ImVec2 textSize = ImGui::CalcTextSize(fullText.c_str());
    float rectWidth = textSize.x + 40.0f;
    float rectHeight = textSize.y + 20.0f;

    // Cores refinadas e combinando
    ImColor shadowColor(0, 0, 0, 90);               // Sombra leve
    ImColor backgroundColor(8, 27, 44, 230);        // Azul petróleo
    ImColor borderColor(0, 255, 255, 255);          // Ciano neon
    ImColor textColor(208, 240, 255, 255);          // Branco-azulado

    ImVec2 rectMin = ImVec2(rectX, rectY);
    ImVec2 rectMax = ImVec2(rectX + rectWidth, rectY + rectHeight);

    // Sombra suave
    draw->AddRectFilled(ImVec2(rectMin.x + 2, rectMin.y + 2), ImVec2(rectMax.x + 2, rectMax.y + 2), shadowColor, 10.0f);

    // Fundo sólido elegante
    draw->AddRectFilled(rectMin, rectMax, backgroundColor, 10.0f);

    // Borda neon
    draw->AddRect(rectMin, rectMax, borderColor, 10.0f, 0, 1.8f);

    // Texto centralizado
    float textPosX = rectX + (rectWidth - textSize.x) * 0.5f;
    float textPosY = rectY + (rectHeight - textSize.y) * 0.5f;
    draw->AddText(ImVec2(textPosX, textPosY), textColor, fullText.c_str());
}
if (!AimbotEsp) return;
if (!Funcoes) return;
void* current_Match = Curent_Match();
void* local_player = GetLocalPlayer(current_Match);
int numVisibleEnemies = 0;
int inimigo_num = 0;
if (local_player != nullptr && current_Match != nullptr) {
monoDictionary<uint8_t *, void **>* players = *(monoDictionary<uint8_t*, void **> **)((long)current_Match + ListPlayer);
void* camera = Camera_main();
if (players != nullptr && camera != nullptr) {
for (int u = 0; u < players->getNumValues(); u++) {
void* closestEnemy = players->getValues()[u];
if (closestEnemy != nullptr && closestEnemy != local_player) {
if (!get_isLocalTeam(closestEnemy)) {
if (get_isVisible(closestEnemy)) {
numVisibleEnemies++;
inimigo_num++;
/*
if (Esqueleto) {
Vector3 enemyPos = getPosition(closestEnemy);
Vector3 enemyScreenPos = WorldToScreenPoint(camera, enemyPos);
if (enemyScreenPos.z < 1) continue;
Vector3 headPos = GetHeadPosition(closestEnemy);
Vector3 chestPos = headPos - Vector3(0, 0.20f, 0);
Vector3 positions[] = {headPos, chestPos, GetLeftAnklePosition(closestEnemy), GetRightAnklePosition(closestEnemy), GetLeftToePosition(closestEnemy), GetRightToePosition(closestEnemy), GetLeftHand(closestEnemy), GetRightHand(closestEnemy)};
Vector3 screenPositions[8];
for (int i = 0; i < 8; i++) {
screenPositions[i] = WorldToScreenPoint(camera, positions[i]);
}
Vector3 headScreenPos = screenPositions[0], chestScreenPos = screenPositions[1], leftAnkleScreenPos = screenPositions[2], rightAnkleScreenPos = screenPositions[3], leftToeScreenPos = screenPositions[4], rightToeScreenPos = screenPositions[5], leftHandScreenPos = screenPositions[6], rightHandScreenPos = screenPositions[7];
draw->AddLine(ImVec2(headScreenPos.x, screenHeight - headScreenPos.y), ImVec2(chestScreenPos.x, screenHeight - chestScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(chestScreenPos.x, screenHeight - chestScreenPos.y), ImVec2(leftAnkleScreenPos.x, screenHeight - leftAnkleScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(chestScreenPos.x, screenHeight - chestScreenPos.y), ImVec2(rightAnkleScreenPos.x, screenHeight - rightAnkleScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(leftAnkleScreenPos.x, screenHeight - leftAnkleScreenPos.y), ImVec2(leftToeScreenPos.x, screenHeight - leftToeScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(rightAnkleScreenPos.x, screenHeight - rightAnkleScreenPos.y), ImVec2(rightToeScreenPos.x, screenHeight - rightToeScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(chestScreenPos.x, screenHeight - chestScreenPos.y), ImVec2(leftHandScreenPos.x, screenHeight - leftHandScreenPos.y), ImColor(255, 0, 0), 2.0f);
draw->AddLine(ImVec2(chestScreenPos.x, screenHeight - chestScreenPos.y), ImVec2(rightHandScreenPos.x, screenHeight - rightHandScreenPos.y), ImColor(255, 0, 0), 2.0f);
}
*/

Vector3 Toepos = getPosition(closestEnemy);
Vector3 Toeposi = WorldToScreenPoint(camera, Vector3(Toepos.x, Toepos.y, Toepos.z));
if (Toeposi.z < 1) continue;
Vector3 HeadPos = getPosition(closestEnemy) + Vector3(0, 1.7f, 0);
Vector3 HeadPosition = WorldToScreenPoint(camera, Vector3(HeadPos.x, HeadPos.y, HeadPos.z));
if (HeadPosition.z < 1) continue;
float distance = Vector3::Distance(getPosition(local_player), HeadPos);
float Hight = abs(HeadPosition.y - Toeposi.y) * (1.2 / 1.1);
float Width = Hight * 0.50f;
Rect rect(HeadPosition.x - Width / 2.f, screenHeight - HeadPosition.y, Width, Hight);


if (currentBoxType != BOX_DISABLED) {
switch (currentBoxType) {
case BOX_TYPE_1:
draw->AddLine(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w * 0.25f, rect.y), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x + rect.w * 0.75f, rect.y), ImVec2(rect.x + rect.w, rect.y), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x + rect.w, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h * 0.25f), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x + rect.w, rect.y + rect.h * 0.75f), ImVec2(rect.x + rect.w, rect.y + rect.h), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x + rect.w, rect.y + rect.h), ImVec2(rect.x + rect.w * 0.75f, rect.y + rect.h), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x + rect.w * 0.25f, rect.y + rect.h), ImVec2(rect.x, rect.y + rect.h), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x, rect.y + rect.h), ImVec2(rect.x, rect.y + rect.h * 0.75f), ImColor(allCores), 2.0f);
draw->AddLine(ImVec2(rect.x, rect.y + rect.h * 0.25f), ImVec2(rect.x, rect.y), ImColor(allCores), 2.0f);
break;
case BOX_TYPE_2:
draw->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), ImColor(colorBox), 0.0f, 0, 2.0f);
break;
case BOX_TYPE_3:
draw->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), ImColor(colorBox), 0.0f, 0, 2.0f);
for (int i = 0; i < rect.h / 2; i++) {
float alpha = (float)(rect.h / 2 - i) / (rect.h / 2);
ImColor gradColor = ImColor(colorBox.x, colorBox.y, colorBox.z, colorBox.w * alpha);
draw->AddLine(ImVec2(rect.x, rect.y + i), ImVec2(rect.x + rect.w, rect.y + i), gradColor, 1.0f);
}
break;
case BOX_TYPE_4:
{
ImColor changingColor = GetChangingColor();
for (int i = 0; i < rect.h / 2; i++) {
float alpha = (float)(rect.h / 2 - i) / (rect.h / 2);
ImColor gradColor = ImColor(changingColor.Value.x, changingColor.Value.y, changingColor.Value.z, changingColor.Value.w * alpha);
draw->AddLine(ImVec2(rect.x, rect.y + rect.h / 2 + i), ImVec2(rect.x + rect.w, rect.y + rect.h / 2 + i), gradColor, 1.0f);
}
draw->AddRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), changingColor, 0.0f, 0, 2.0f);
}
break;
default:
break;
}
}

if (Vida) {
float maxHealth = get_MaxHP(closestEnemy);
float currentHealth = GetHp(closestEnemy);
bool isKnocked = get_IsDieing(closestEnemy);
float healthPercentage = currentHealth / maxHealth;
ImVec4 borderColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
ImVec4 fillColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
float barWidth = 5.0f;
ImVec2 barPos(rect.x - 10.0f, rect.y);
ImVec2 barSize(barWidth, rect.h * healthPercentage);
if (isKnocked) {
fillColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
} else {
ImVec4 healthColor = ImLerp(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ImVec4(0.0f, 1.0f, 0.0f, 1.0f), healthPercentage);
fillColor = healthColor;
}
draw->AddRectFilled(ImVec2(barPos.x, barPos.y), ImVec2(barPos.x + barWidth, barPos.y + rect.h), ImColor(0, 0, 0, 255));
draw->AddRectFilled(ImVec2(barPos.x, barPos.y + rect.h - barSize.y), ImVec2(barPos.x + barWidth, barPos.y + rect.h), ImColor(fillColor));
draw->AddRect(ImVec2(barPos.x, barPos.y), ImVec2(barPos.x + barWidth, barPos.y + rect.h), ImColor(borderColor));
}
if (Distancia) {
std::string distanceText = "[" + std::to_string((int)distance) + "M]";
ImVec2 textSize = ImGui::CalcTextSize(distanceText.c_str(), NULL, 0.8f * ImGui::GetFontSize());
ImVec2 textPos(rect.x + rect.w / 2.0f - textSize.x / 2.0f, rect.y + rect.h + 10);
draw->AddRectFilled(ImVec2(textPos.x - 5, textPos.y - 5), ImVec2(textPos.x + textSize.x + 5, textPos.y + textSize.y + 5), ImColor(0, 0, 0, 255), 5.0f);
draw->AddText(ImVec2(textPos.x, textPos.y), ImColor(colorDistance), distanceText.c_str());
}

if (Nick) {
std::string names;
    monoString* Nick = get_NickName(closestEnemy);
    if (Nick != nullptr) {
        int nick_Len = Nick->getLength();
        for (int i = 0; i < nick_Len; i++) {
            char data = get_Chars(Nick, i);
            names += isascii(data) ? data : '?';
        }
    }

ImVec2 textSize = ImGui::CalcTextSize(names.c_str(), NULL, 0.8f * ImGui::GetFontSize());
ImVec2 textPos(rect.x + rect.w / 2.0f - textSize.x / 2.0f, rect.y + rect.h + 30);
draw->AddRectFilled(ImVec2(textPos.x - 5, textPos.y - 5), ImVec2(textPos.x + textSize.x + 5, textPos.y + textSize.y + 5), ImColor(0, 0, 0, 255), 5.0f);
draw->AddText(ImVec2(textPos.x, textPos.y), ImColor(colorNick), names.c_str());
}

if (NewInfoPlayer) {
std::string enemyNumber = std::to_string(inimigo_num);
monoString* Nick = get_NickName(closestEnemy);
std::string names;
if (Nick != nullptr) {
int nick_Len = Nick->getLength();
for (int i = 0; i < nick_Len; i++) {
char32_t data = get_Chars(Nick, i);
names += data;
}
}
std::string distanceText = std::to_string((int)distance) + "m";
ImVec2 textSizeNumber = ImGui::CalcTextSize(enemyNumber.c_str());
ImVec2 textSizeName = ImGui::CalcTextSize(names.c_str());
ImVec2 textSizeDistance = ImGui::CalcTextSize(distanceText.c_str());
ImVec2 padding(5, 5);
ImVec2 squareSize(20, 20);
float totalWidth = textSizeNumber.x + padding.x + squareSize.x + textSizeName.x + padding.x + textSizeDistance.x + 3 * padding.x;
ImVec2 rectPos(rect.x + rect.w / 2.0f - totalWidth / 2.0f, rect.y - squareSize.y - padding.y - 10.0f);
ImVec2 rectSize(totalWidth, squareSize.y + 2 * padding.y);
draw->AddRectFilled(rectPos, ImVec2(rectPos.x + rectSize.x, rectPos.y + rectSize.y), ImColor(0, 0, 0, 204), 2.0f);
ImVec2 numPos(rectPos.x + padding.x, rectPos.y + padding.y);
draw->AddRectFilled(numPos, ImVec2(numPos.x + squareSize.x, numPos.y + squareSize.y), ImColor(allCores), 4.0f);
draw->AddRect(numPos, ImVec2(numPos.x + squareSize.x, numPos.y + squareSize.y), ImColor(0, 0, 0, 204), 4.0f, 0, 2.0f);
draw->AddText(ImVec2(numPos.x + (squareSize.x - textSizeNumber.x) / 2.0f, numPos.y + (squareSize.y - textSizeNumber.y) / 2.0f), ImColor(255, 255, 255, 255), enemyNumber.c_str());
ImVec2 namePos(numPos.x + squareSize.x + padding.x, numPos.y + (squareSize.y - textSizeName.y) / 2.0f);
draw->AddText(namePos, ImColor(255, 255, 255, 255), names.c_str());
ImVec2 distancePos(namePos.x + textSizeName.x + padding.x, namePos.y);
draw->AddText(distancePos, ImColor(255, 255, 255, 255), distanceText.c_str());
}


bool isKnocked = get_IsDieing(closestEnemy);
if (currentLinePosition != TOP_LEFT) {
ImVec2 startPoint;
switch (currentLinePosition) {
case TOP_LEFT: startPoint = ImVec2(10, 10); break;
case TOP: startPoint = ImVec2(screenWidth / 2.0f, 10); break;
case BOTTOM: startPoint = ImVec2(screenWidth / 2.0f, screenHeight - 10); break;
case CENTER: startPoint = ImVec2(screenWidth / 2.0f, screenHeight / 2.0f); break;
default: startPoint = ImVec2(10, 10); break;
}
ImVec4 colorsRight[] = { ImVec4(0, 0, 0, 1), ImVec4(0, 0, 0, 1), ImVec4(allCores), ImVec4(allCores), ImVec4(allCores) };
ImVec4 colorsLeft[] = { ImVec4(0, 0, 0, 1), ImVec4(0, 0, 0, 1), ImVec4(allCores2), ImVec4(allCores2), ImVec4(allCores2) };
int numColors = sizeof(colorsRight) / sizeof(colorsRight[0]);
int segments = 400;
ImVec4* selectedColors = (HeadPosition.x < screenWidth / 2) ? colorsLeft : colorsRight;
if (isKnocked) {
ImVec4 derrubadosColor = ImVec4(DerrubadosColor);
draw->AddLine(ImVec2(startPoint.x, startPoint.y), ImVec2(HeadPosition.x, screenHeight - HeadPosition.y), ImColor(derrubadosColor), 2.0f);
} else {
for (int i = 0; i < segments; ++i) {
float t = i / float(segments);
ImVec2 segmentStart = ImLerp(startPoint, ImVec2(HeadPosition.x, screenHeight - HeadPosition.y), t);
ImVec2 segmentEnd = ImLerp(startPoint, ImVec2(HeadPosition.x, screenHeight - HeadPosition.y), t + 1.0f / segments);
float colorIndex = t * (numColors * 2 - 1);
int colorStartIndex = static_cast<int>(colorIndex) % numColors;
int colorEndIndex = (colorStartIndex + 1) % numColors;
float colorAlpha = colorIndex - static_cast<int>(colorIndex);
ImVec4 color = ImVec4(
ImLerp(selectedColors[colorStartIndex].x, selectedColors[colorEndIndex].x, colorAlpha),
ImLerp(selectedColors[colorStartIndex].y, selectedColors[colorEndIndex].y, colorAlpha),
ImLerp(selectedColors[colorStartIndex].z, selectedColors[colorEndIndex].z, colorAlpha),
1.0f
);
draw->AddLine(segmentStart, segmentEnd, ImColor(color), 2.0f);
}
}

if (NewLaser) {
float lineThickness = 4.0f;
for (int u = 0; u < players->getNumValues(); u++) {
void* closestEnemy = players->getValues()[u];
if (closestEnemy != nullptr && closestEnemy != local_player) {
if (!get_isLocalTeam(closestEnemy) && get_isVisible(closestEnemy)) {
Vector3 HeadPos = getPosition(closestEnemy) + Vector3(0, 1.9f, 0);
Vector3 HeadPosition = WorldToScreenPoint(camera, Vector3(HeadPos.x, HeadPos.y, HeadPos.z));
if (HeadPosition.z < 1) continue;
float startX = HeadPosition.x;
float startY = screenHeight - HeadPosition.y;
float endY = 0;
draw->AddLine(ImVec2(startX - 2, startY), ImVec2(startX - 2, endY), ImColor(shadowColor), lineThickness);
draw->AddLine(ImVec2(startX + 2, startY), ImVec2(startX + 2, endY), ImColor(shadowColor), lineThickness);
draw->AddLine(ImVec2(startX, startY), ImVec2(startX, endY), ImColor(laserColor), lineThickness);
}
}
}
}
}


if (DadosEnemy) {
    float maxHealth = get_MaxHP(closestEnemy);
    float currentHealth = GetHp(closestEnemy);
    float distanceValue = distance;
    std::string vidaText = "Vida: " + std::to_string(static_cast<int>(currentHealth)) + "HP";
    monoString* Nick = get_NickName(closestEnemy);
    std::string nomeText = "Nome: ";
    if (Nick != nullptr) {
        int nick_Len = Nick->getLength();
        for (int i = 0; i < nick_Len; i++) {
            char32_t data = get_Chars(Nick, i);
            nomeText += data;
        }
    }
    std::string estado = get_IsDieing(closestEnemy) ? "Caído" : "Vivo";
    std::string estadoText = "Estado: " + estado;
    std::string distanciaText = "Distância: " + std::to_string(static_cast<int>(distanceValue)) + " metros";
    std::string outrosInimigosText = "Outros inimigos: " + std::to_string(numVisibleEnemies) + " detectado";   
    ImVec2 textSizeVida = ImGui::CalcTextSize(vidaText.c_str());
    ImVec2 textSizeNome = ImGui::CalcTextSize(nomeText.c_str());
    ImVec2 textSizeDistancia = ImGui::CalcTextSize(distanciaText.c_str());
    ImVec2 textSizeOutros = ImGui::CalcTextSize(outrosInimigosText.c_str());
    ImVec2 textSizeEstado = ImGui::CalcTextSize(estadoText.c_str());  
    ImVec2 padding(5, 5);
    ImVec2 rectSize(std::max({textSizeVida.x, textSizeNome.x, textSizeDistancia.x, textSizeOutros.x, textSizeEstado.x}) + 2 * padding.x,
textSizeEstado.y + textSizeVida.y + textSizeNome.y + textSizeDistancia.y + textSizeOutros.y + 5 * padding.y);
    ImVec2 rectPos((ImGui::GetIO().DisplaySize.x - rectSize.x) / 2.0f, ImGui::GetIO().DisplaySize.y - rectSize.y - 20.0f);  
    draw->AddRectFilled(rectPos, ImVec2(rectPos.x + rectSize.x, rectPos.y + rectSize.y), ImColor(0, 0, 0, 255), 5.0f);
   draw->AddText(ImVec2(rectPos.x + padding.x, rectPos.y + padding.y), ImColor(255, 255, 255, 255), estadoText.c_str());
draw->AddText(ImVec2(rectPos.x + padding.x, rectPos.y + padding.y + textSizeEstado.y), ImColor(255, 255, 255, 255), vidaText.c_str());
draw->AddText(ImVec2(rectPos.x + padding.x, rectPos.y + padding.y + textSizeEstado.y + textSizeVida.y), ImColor(255, 255, 255, 255), nomeText.c_str());
draw->AddText(ImVec2(rectPos.x + padding.x, rectPos.y + padding.y + textSizeEstado.y + textSizeVida.y + textSizeNome.y), ImColor(255, 255, 255, 255), distanciaText.c_str());
draw->AddText(ImVec2(rectPos.x + padding.x, rectPos.y + padding.y + textSizeEstado.y + textSizeVida.y + textSizeNome.y + textSizeDistancia.y), ImColor(255, 255, 255, 255), outrosInimigosText.c_str());
}
}
}
}
}
}

#define ICON_FA_TELEGRAM "\xef\x8b\x86"	// U+f2c6
#define ICON_FA_EXCLAMATION_TRIANGLE "\xef\x81\xb1" 


{
 
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    // The text to be displayed
    std::string topText = " TOTAL ENEMIES - ";
    std::string enemiesCount = std::to_string(numVisibleEnemies);
    std::string fullText = topText + enemiesCount + " ";

    // The icon string
    std::string icon = ICON_FA_EXCLAMATION_TRIANGLE " ";

    // Calculate the size of the main text
    ImVec2 topTextSize = ImGui::CalcTextSize(fullText.c_str());

    // Calculate the size of the icon (we'll use this for positioning)
    ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());

    // Add extra padding to increase the space around the text
    float horizontalPadding = 10.0f; 
    float verticalPadding = 15.0f;

    // Calculate the total width of the text and two icons, including padding
    float totalWidth = (iconSize.x * 2) + topTextSize.x + (horizontalPadding * 2);

    // Calculate the starting position for the entire block
    ImVec2 entireBlockPos(screenWidth / 2.0f - totalWidth / 2.0f, 65);

    // Background rectangle, positioned only behind the text with extra padding
    ImVec2 backgroundStart(entireBlockPos.x + iconSize.x + (horizontalPadding / 2), entireBlockPos.y - verticalPadding);
    ImVec2 backgroundEnd(entireBlockPos.x + iconSize.x + topTextSize.x + (horizontalPadding * 1.5f), entireBlockPos.y + topTextSize.y + verticalPadding);
//    draw->AddRectFilled(backgroundStart, backgroundEnd, ImColor(250, 0, 0, 200), 6.0f);
    
    
//   ImVec2 topTextSize = ImGui::CalcTextSize(topText.c_str());
    ImVec2 topTextPos(screenWidth / 2.0f - topTextSize.x / 2.0f, 65);
    //auto draw = ImGui::GetBackgroundDrawList();

    // Background rectangle
    draw->AddRectFilled(ImVec2(topTextPos.x - 5, topTextPos.y - 5), 
    ImVec2(topTextPos.x + topTextSize.x + 5, topTextPos.y + topTextSize.y + 8), 
    ImColor(220, 4, 5, 200), 3.0f);
    
    // Draw the left icon in yellow
    draw->AddText(NULL, 30.0f, entireBlockPos, ImColor(255, 255, 0, 255), icon.c_str());

    // Draw the gradient text
    float xPos = entireBlockPos.x + iconSize.x + horizontalPadding;
    static float time = 0.0f;
    time += ImGui::GetIO().DeltaTime * 2.0f;

    const char* gradientText = topText.c_str();
    for (int i = 0; gradientText[i] != '\0'; i++) {
        char currentChar[2] = { gradientText[i], '\0' };
        float hue = (float)i / strlen(gradientText) + time;
        ImVec4 color = ImVec4(
            sinf(hue * 2.0f) * 0.5f + 0.5f,
            sinf(hue * 3.0f) * 0.5f + 0.5f,
            sinf(hue * 4.0f) * 0.5f + 0.5f,
            1.0f
        );
        draw->AddText(NULL, 30.0f, ImVec2(xPos, entireBlockPos.y), ImColor(255, 255, 255), currentChar);
        xPos += ImGui::CalcTextSize(currentChar).x;
    }

    // Draw the enemy count with a solid color (white in this case)
    draw->AddText(NULL, 30.0f, ImVec2(xPos, entireBlockPos.y), ImColor(255, 255, 255), enemiesCount.c_str());

    // Update the position for the next element
    xPos += ImGui::CalcTextSize(enemiesCount.c_str()).x;

    // Add extra horizontal padding to the right icon's position
    xPos += horizontalPadding;

    // Draw the right icon in yellow
    draw->AddText(NULL, 30.0f, ImVec2(xPos, entireBlockPos.y), ImColor(255, 255, 0, 255), icon.c_str());
}

}
}

