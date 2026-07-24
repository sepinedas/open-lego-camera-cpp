// Offscreen render of the welcome screen (Lego-brick camera + Start/Sleep) to a
// PNG, using the real icons.cpp / ui.cpp code so the graphic can be eyeballed
// without a Pi or a display. Not part of the app build.
//
//   g++ -std=c++17 tools/welcome_mockup.cpp src/icons.cpp src/ui.cpp \
//       $(pkg-config --cflags --libs sdl2 SDL2_gfx opencv4) -o /tmp/welcome_mockup
#include <algorithm>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/ui.hpp"
#include "../src/icons.hpp"

using namespace olc;

static cv::Mat surfaceToMat(SDL_Surface* s) {
    cv::Mat m(s->h, s->w, CV_8UC4, s->pixels, s->pitch), bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR);
    return bgr.clone();
}

// Mirror App::drawText: render 8x8 text scaled up.
static void text(SDL_Renderer* r, int x, int topY, const std::string& s,
                 int scale, SDL_Color c) {
    int w = 8 * (int)s.size();
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET, w, 8);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    stringRGBA(r, 0, 0, s.c_str(), c.r, c.g, c.b, c.a);
    SDL_SetRenderTarget(r, nullptr);
    SDL_Rect dst{x - (w * scale) / 2, topY, w * scale, 8 * scale};
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

static cv::Mat renderWelcome(int W, int H) {
    // Self-contained SDL session per render: reusing target textures across two
    // software renderers in one session drops the scaled text, so init/quit here.
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface* surf =
        SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (int y = 0; y < H; ++y) {
        double f = H > 1 ? (double)y / (H - 1) : 0.0;
        SDL_SetRenderDrawColor(r, (Uint8)(18 * (1 - f) + 6 * f),
                               (Uint8)(22 * (1 - f) + 7 * f),
                               (Uint8)(44 * (1 - f) + 14 * f), 255);
        SDL_RenderDrawLine(r, 0, y, W, y);
    }

    const std::string title = "OPEN LEGO CAMERA";
    int fitW = (int)(W * 0.86 / (8 * (int)title.size()));
    int tscale = std::max(2, std::min({fitW, H / 60, 6}));
    text(r, W / 2, std::max(14, H / 14), title, tscale, {255, 214, 40, 245});

    double unit = std::min(W / 13.0, H / 12.0);
    drawLegoCamera(r, W / 2, (int)(H * 0.42), unit, 255);

    Menu menu;
    menu.wake();
    auto btns = menu.layout(Mode::Welcome, W, H, false);
    for (const auto& b : btns) Menu::drawButton(r, b, 255, false);
    int lscale = std::max(2, std::min(H / 220, 3));
    for (const auto& b : btns)
        text(r, b.cx, b.cy + b.r + 12,
             b.action == Action::StartCamera ? "START" : "SLEEP", lscale,
             {255, 255, 255, 235});

    const std::string hint = "DOUBLE-TAP SCREEN TO WAKE FROM SLEEP";
    int hFit = (int)(W * 0.9 / (8 * (int)hint.size()));
    int hscale = std::max(1, std::min(hFit, 2));
    text(r, W / 2, H - 16 - 8 * hscale, hint, hscale, {150, 160, 180, 200});

    cv::Mat out = surfaceToMat(surf);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return out;
}

int main() {
    cv::Mat land = renderWelcome(800, 480);   // typical Pi touchscreen (landscape)
    cv::Mat port = renderWelcome(480, 800);   // rotated (portrait)
    cv::imwrite("/tmp/welcome_landscape.png", land);
    cv::imwrite("/tmp/welcome_portrait.png", port);
    return 0;
}
