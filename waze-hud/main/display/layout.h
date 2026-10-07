#pragma once

#include <cstdint>

namespace waze_hud {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

namespace layout {
constexpr int Width = 320;
constexpr int Height = 170;
constexpr Rect Full{0, 0, 320, 170};
constexpr int MaxRegionPixels = Width * Height;
}  // namespace layout

}  // namespace waze_hud
