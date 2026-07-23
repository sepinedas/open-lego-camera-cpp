// Verifies the display-rotation math from app.cpp: draw the UI to a logical
// canvas, then blit it rotated onto the panel exactly like App::present().
#include <SDL2/SDL.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/ui.hpp"
using namespace olc;

static void drawFakePreview(SDL_Renderer* r, int w, int h) {
    for (int y = 0; y < h; ++y) {
        Uint8 v = (Uint8)(40 + 120 * y / h);
        SDL_SetRenderDrawColor(r, v / 2, v, (Uint8)(200 - v / 2), 255);
        SDL_RenderDrawLine(r, 0, y, w, y);
    }
    // A marker in the TOP-LEFT of the logical view so rotation is obvious.
    SDL_SetRenderDrawColor(r, 240, 60, 60, 255);
    SDL_Rect box{10, 10, 90, 50};
    SDL_RenderFillRect(r, &box);
}

// screenW/H = physical panel; rotate = UI rotation. Mirrors App::present().
static cv::Mat renderRotated(int screenW, int screenH, int rotate) {
    bool swap = (rotate == 90 || rotate == 270);
    int viewW = swap ? screenH : screenW;
    int viewH = swap ? screenW : screenH;

    SDL_Surface* screen = SDL_CreateRGBSurfaceWithFormat(0, screenW, screenH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(screen);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture* canvas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, viewW, viewH);

    // Draw the UI into the logical canvas.
    SDL_SetRenderTarget(r, canvas);
    drawFakePreview(r, viewW, viewH);
    Menu menu; menu.wake();
    for (const auto& b : menu.layout(Mode::Camera, viewW, viewH, false))
        Menu::drawButton(r, b, 255, false);

    // Blit rotated onto the panel (identical to App::present()).
    SDL_SetRenderTarget(r, nullptr);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_Rect dst{(screenW - viewW) / 2, (screenH - viewH) / 2, viewW, viewH};
    SDL_RenderCopyEx(r, canvas, nullptr, &dst, (double)rotate, nullptr, SDL_FLIP_NONE);

    cv::Mat m(screen->h, screen->w, CV_8UC4, screen->pixels, screen->pitch), bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR);
    cv::Mat out = bgr.clone();
    SDL_DestroyTexture(canvas);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(screen);
    return out;
}

int main() {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("init: %s", SDL_GetError()); return 1; }
    const int W = 800, H = 480; // physical panel (landscape)

    auto label = [](cv::Mat& m, const std::string& t) {
        cv::putText(m, t, {16, H - 16}, cv::FONT_HERSHEY_SIMPLEX, 0.7, {255, 255, 255}, 2);
    };
    cv::Mat r0 = renderRotated(W, H, 0);   label(r0, "rotate 0");
    cv::Mat r90 = renderRotated(W, H, 90);  label(r90, "rotate 90");
    cv::Mat r270 = renderRotated(W, H, 270); label(r270, "rotate 270");

    cv::Mat top, sheet;
    cv::hconcat(r0, r90, top);
    cv::hconcat(top, r270, sheet);
    cv::imwrite("/home/user/open-lego-camera-cpp/build/rotate-test.png", sheet);
    SDL_Quit();
    return 0;
}
