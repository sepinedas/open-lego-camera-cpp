#pragma once

#include <SDL2/SDL.h>

#include "types.hpp"

namespace olc {

// Draws the icon for `action` centred at (cx, cy) inside a circle of radius r.
// `alpha` is the current menu-fade opacity (0..255) and modulates every icon so
// the whole menu fades as one. `recording` swaps the Record icon between the
// idle dot and the stop square. All icons are pure vector shapes, no text.
void drawIcon(SDL_Renderer* ren, Action action, int cx, int cy, int r,
              Uint8 alpha, bool recording);

// Draws the decorative "camera built from Lego bricks" used on the welcome
// screen, centred at (cx, cy). `unit` is the brick-stud pitch in pixels (the
// whole graphic scales with it); `alpha` fades it in/out as one.
void drawLegoCamera(SDL_Renderer* ren, int cx, int cy, double unit, Uint8 alpha);

} // namespace olc
