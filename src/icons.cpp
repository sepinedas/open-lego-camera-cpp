#include "icons.hpp"

#include <algorithm>
#include <cmath>

#include <SDL2/SDL2_gfxPrimitives.h>

namespace olc {
namespace {

// Foreground white for most icons; specific ones override the colour.
constexpr Uint8 kFg = 240;

Uint8 mod(Uint8 base, Uint8 alpha) {
    return static_cast<Uint8>((int)base * (int)alpha / 255);
}

// A ring (hollow circle) of the given stroke thickness.
void ring(SDL_Renderer* r, int cx, int cy, int rad, int thick,
          Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a) {
    for (int i = 0; i < thick; ++i)
        aacircleRGBA(r, cx, cy, rad - i, cr, cg, cb, a);
}

void iconShutter(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    ring(r, cx, cy, rad, 3, c, c, c, a);
    filledCircleRGBA(r, cx, cy, rad - 6, c, c, c, a);
}

void iconRecord(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a, bool recording) {
    Uint8 av = a; // red dot / red stop square
    if (recording) {
        int h = (int)(rad * 0.62);
        roundedBoxRGBA(r, cx - h, cy - h, cx + h, cy + h, 3, 235, 60, 60, av);
    } else {
        filledCircleRGBA(r, cx, cy, rad - 3, 235, 60, 60, av);
        aacircleRGBA(r, cx, cy, rad - 3, 235, 60, 60, av);
    }
}

// Magnifier with a "+" or "-" in the lens.
void iconZoom(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a, bool plus) {
    Uint8 c = mod(kFg, a);
    int lens = (int)(rad * 0.62);
    int lx = cx - (int)(rad * 0.22);
    int ly = cy - (int)(rad * 0.22);
    ring(r, lx, ly, lens, 3, c, c, c, a);
    // handle
    int hx = lx + (int)(lens * 0.72);
    int hy = ly + (int)(lens * 0.72);
    thickLineRGBA(r, hx, hy, cx + rad - 2, cy + rad - 2, 4, c, c, c, a);
    // + / -
    int s = (int)(lens * 0.5);
    thickLineRGBA(r, lx - s, ly, lx + s, ly, 3, c, c, c, a);
    if (plus) thickLineRGBA(r, lx, ly - s, lx, ly + s, 3, c, c, c, a);
}

// Framed landscape (photo) icon: rectangle with a sun and a hill.
void iconGallery(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    int w = (int)(rad * 0.95), h = (int)(rad * 0.72);
    roundedRectangleRGBA(r, cx - w, cy - h, cx + w, cy + h, 4, c, c, c, a);
    filledCircleRGBA(r, cx - w / 2, cy - h / 3, (int)(rad * 0.16), c, c, c, a);
    // hill (triangle) rising to the frame's right edge
    filledTrigonRGBA(r, cx - w + 3, cy + h - 2,
                        cx + (int)(w * 0.2), cy - (int)(h * 0.1),
                        cx + w - 3, cy + h - 2, c, c, c, a);
}

void iconBack(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    int s = (int)(rad * 0.55);
    thickLineRGBA(r, cx + s / 2, cy - s, cx - s, cy, 4, c, c, c, a);
    thickLineRGBA(r, cx - s, cy, cx + s / 2, cy + s, 4, c, c, c, a);
}

// Left/right navigation triangle.
void iconNav(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a, bool left) {
    Uint8 c = mod(kFg, a);
    int s = (int)(rad * 0.6);
    if (left)
        filledTrigonRGBA(r, cx + s, cy - s, cx + s, cy + s, cx - s, cy, c, c, c, a);
    else
        filledTrigonRGBA(r, cx - s, cy - s, cx - s, cy + s, cx + s, cy, c, c, c, a);
}

// Play triangle inside a ring.
void iconPlay(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    ring(r, cx, cy, rad, 3, c, c, c, a);
    int s = (int)(rad * 0.42);
    filledTrigonRGBA(r, cx - s + 2, cy - s, cx - s + 2, cy + s, cx + s + 2, cy, c, c, c, a);
}

void iconTrash(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    int w = (int)(rad * 0.6), top = cy - (int)(rad * 0.5), bot = cy + (int)(rad * 0.6);
    // lid + handle
    thickLineRGBA(r, cx - w - 3, top, cx + w + 3, top, 3, c, c, c, a);
    thickLineRGBA(r, cx - w / 2, top, cx + w / 2, top - (int)(rad * 0.22), 3, c, c, c, a);
    thickLineRGBA(r, cx + w / 2, top, cx + w / 2, top - (int)(rad * 0.22), 3, c, c, c, a);
    // body
    roundedRectangleRGBA(r, cx - w, top + 4, cx + w, bot, 3, c, c, c, a);
    // ribs
    for (int i = -1; i <= 1; ++i)
        thickLineRGBA(r, cx + i * (w / 2), top + 9, cx + i * (w / 2), bot - 4, 2, c, c, c, a);
}

// Smiley face: the live facial-filter (cycle) button.
void iconFilter(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    Uint8 c = mod(kFg, a);
    ring(r, cx, cy, rad, 3, c, c, c, a);
    int eo = (int)(rad * 0.34), ey = cy - (int)(rad * 0.20);
    int er = std::max(2, (int)(rad * 0.12));
    filledCircleRGBA(r, cx - eo, ey, er, c, c, c, a);
    filledCircleRGBA(r, cx + eo, ey, er, c, c, c, a);
    // Upturned smile: an arc approximated by short chords across the lower face.
    int sr = (int)(rad * 0.5);
    int scy = cy - (int)(rad * 0.10);
    int px = cx - sr, py = scy;
    for (int deg = 20; deg <= 160; deg += 20) {
        double t = deg * 3.14159265358979 / 180.0;
        int nx = cx + (int)(sr * -std::cos(t));
        int ny = scy + (int)(sr * std::sin(t) * 0.85);
        thickLineRGBA(r, px, py, nx, ny, 3, c, c, c, a);
        px = nx;
        py = ny;
    }
}

void iconCheck(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    int s = (int)(rad * 0.55);
    thickLineRGBA(r, cx - s, cy, cx - s / 4, cy + s, 5, 90, 210, 110, a);
    thickLineRGBA(r, cx - s / 4, cy + s, cx + s, cy - s, 5, 90, 210, 110, a);
}

void iconCross(SDL_Renderer* r, int cx, int cy, int rad, Uint8 a) {
    int s = (int)(rad * 0.5);
    thickLineRGBA(r, cx - s, cy - s, cx + s, cy + s, 5, 235, 80, 80, a);
    thickLineRGBA(r, cx + s, cy - s, cx - s, cy + s, 5, 235, 80, 80, a);
}

} // namespace

void drawIcon(SDL_Renderer* ren, Action action, int cx, int cy, int r,
              Uint8 alpha, bool recording) {
    switch (action) {
        case Action::Shutter:     iconShutter(ren, cx, cy, r, alpha); break;
        case Action::Record:      iconRecord(ren, cx, cy, r, alpha, recording); break;
        case Action::ZoomIn:      iconZoom(ren, cx, cy, r, alpha, true); break;
        case Action::ZoomOut:     iconZoom(ren, cx, cy, r, alpha, false); break;
        case Action::OpenGallery: iconGallery(ren, cx, cy, r, alpha); break;
        case Action::Back:        iconBack(ren, cx, cy, r, alpha); break;
        case Action::Prev:        iconNav(ren, cx, cy, r, alpha, true); break;
        case Action::Next:        iconNav(ren, cx, cy, r, alpha, false); break;
        case Action::Play:        iconPlay(ren, cx, cy, r, alpha); break;
        case Action::Delete:      iconTrash(ren, cx, cy, r, alpha); break;
        case Action::ConfirmYes:  iconCheck(ren, cx, cy, r, alpha); break;
        case Action::ConfirmNo:   iconCross(ren, cx, cy, r, alpha); break;
        case Action::CycleFilter: iconFilter(ren, cx, cy, r, alpha); break;
        default: break;
    }
}

} // namespace olc
