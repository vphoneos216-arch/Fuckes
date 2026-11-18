#include "imagens/logo.h"
#include "imagens/esp.h"
#include "imagens/aimbot.h"
#include "imagens/ff.h"
#include "imagens/cogwheel.h"
#include "imagens/user.h"
#include "imagens/ic_error.h"
#include "imagens/exitt.h"
#include "imagens/target.h"
#include "stb_image.h"

    struct TextureInfo { ImTextureID textureId; int x; int y; int w; int h; };
    void DrawImage(int x, int y, int w, int h, ImTextureID Texture) {
    ImGui::GetForegroundDrawList()->AddImage(Texture, ImVec2(x, y), ImVec2(x + w, y + h));}   

    static TextureInfo textureInfo;
    TextureInfo createTexture(char *ImagePath) {
    int w, h, n;
    stbi_uc *data = stbi_load(ImagePath, &w, &h, &n, 0);
    GLuint texture;
    glGenTextures(1, &texture);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (n == 3) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }stbi_image_free(data);
    textureInfo.textureId = reinterpret_cast<ImTextureID>((GLuint *) texture);
    textureInfo.w = w;
    textureInfo.h = h;
    return textureInfo;}

    TextureInfo CreateTexture(const unsigned char* buf, int len) {
    TextureInfo image;
    unsigned char* image_data = stbi_load_from_memory(buf, len, &image.w, &image.h, NULL, 0);
    if (image_data == NULL) {perror("File does not exist");}
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    #endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.w, image.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);
    image.textureId = (ImTextureID)image_texture;
    return image;}

TextureInfo logo;
TextureInfo esp;
TextureInfo aimbot;
TextureInfo ff;
TextureInfo cogwheel;
TextureInfo user;
TextureInfo ic_error;
TextureInfo lnd;
extern TextureInfo ff;
void InitTexture() {
logo = CreateTexture(logo_data, sizeof(logo_data));
esp = CreateTexture(esp_data, sizeof(esp_data));
aimbot = CreateTexture(aimbot_data, sizeof(aimbot_data));
ff = CreateTexture(ff_data, sizeof(ff_data));
cogwheel = CreateTexture(cogwheel_data, sizeof(cogwheel_data));
user = CreateTexture(user_data, sizeof(user_data));
ic_error = CreateTexture(ic_error_data, sizeof(ic_error_data));
lnd = CreateTexture(target_data, sizeof(target_data));
}





