#pragma once
// OG bottom white bar — menu open handle (Bar mode only)
namespace ui {
namespace bar {
    inline bool  g_open       = false;
    inline bool  g_closed     = true;
    inline float g_alpha      = 0.f;
    inline float g_game_alpha = 1.f;
    inline float g_sw         = 0.f;
    inline float g_sh         = 0.f;

    void render();
    inline void set_game_alpha(float a) { g_game_alpha = a; }
    inline float game_alpha() { return g_game_alpha; }
}
}
