#include "ui.hpp"

#include <algorithm>
#include <cmath>

#include <SDL2/SDL2_gfxPrimitives.h>

#include "icons.hpp"

namespace olc {

namespace {
constexpr Uint32 kVisibleMs = 3000; // fully shown after the last interaction
constexpr Uint32 kFadeMs = 500;     // then fades out over this long

// Lay a horizontal row of equally spaced circular buttons, centred near the
// bottom of the screen.
std::vector<Button> row(const std::vector<Action>& actions, int sw, int sh) {
    std::vector<Button> out;
    if (actions.empty()) return out;
    int n = (int)actions.size();

    // Size the discs off the smaller screen dimension, then shrink so the whole
    // row (pitch = 2r + gap, gap = r  ->  total width = r*(3n-1)) fits across the
    // width with a small margin. This keeps all buttons on-screen in both
    // landscape and rotated (portrait) layouts.
    int r = std::max(24, std::min(sw, sh) / 14);
    int rFit = (int)(0.96 * sw / (3 * n - 1));
    r = std::max(16, std::min(r, rFit));

    int gap = r;
    int pitch = 2 * r + gap;
    int total = pitch * n - gap;
    int x0 = sw / 2 - total / 2 + r;
    int y = sh - r - std::max(20, sh / 24);
    for (int i = 0; i < n; ++i)
        out.push_back({actions[i], x0 + i * pitch, y, r});
    return out;
}
} // namespace

Uint8 Menu::alpha() const {
    Uint32 elapsed = SDL_GetTicks() - lastActivity_;
    if (elapsed < kVisibleMs) return 255;
    if (elapsed < kVisibleMs + kFadeMs) {
        float t = (float)(elapsed - kVisibleMs) / (float)kFadeMs;
        return (Uint8)std::lround(255.0f * (1.0f - t));
    }
    return 0;
}

std::vector<Button> Menu::layout(Mode mode, int sw, int sh, bool hasVideo) const {
    switch (mode) {
        case Mode::Camera:
            // Zoom is pinch-to-zoom (two fingers), so the row is filter /
            // gallery / shutter / record.
            return row({Action::CycleFilter, Action::OpenGallery,
                        Action::Shutter, Action::Record},
                       sw, sh);
        case Mode::Gallery: {
            std::vector<Action> a = {Action::Back, Action::Prev};
            if (hasVideo) a.push_back(Action::Play);
            a.push_back(Action::Next);
            a.push_back(Action::Delete);
            return row(a, sw, sh);
        }
        case Mode::ConfirmDelete: {
            // Two large, always-visible buttons centred on screen; size off the
            // smaller dimension so both fit side by side in portrait too.
            int r = std::max(46, std::min(sw, sh) / 6);
            int y = sh / 2;
            int dx = r + r / 2 + 20;
            return {{Action::ConfirmNo, sw / 2 - dx, y, r},
                    {Action::ConfirmYes, sw / 2 + dx, y, r}};
        }
        case Mode::Playback:
            return {}; // tap anywhere to stop
    }
    return {};
}

void Menu::drawButton(SDL_Renderer* ren, const Button& b, Uint8 alpha,
                      bool recording) {
    Uint8 bg = (Uint8)((int)alpha * 42 / 100);   // ~0.42 * alpha
    Uint8 ring = (Uint8)((int)alpha * 28 / 100);
    filledCircleRGBA(ren, b.cx, b.cy, b.r, 18, 18, 24, bg);
    aacircleRGBA(ren, b.cx, b.cy, b.r, 255, 255, 255, ring);
    drawIcon(ren, b.action, b.cx, b.cy, (int)(b.r * 0.62), alpha, recording);
}

Action Menu::hitTest(const std::vector<Button>& buttons, int x, int y) {
    for (const auto& b : buttons) {
        int dx = x - b.cx, dy = y - b.cy;
        int pad = b.r + b.r / 4; // generous target for fingers
        if (dx * dx + dy * dy <= pad * pad) return b.action;
    }
    return Action::None;
}

} // namespace olc
