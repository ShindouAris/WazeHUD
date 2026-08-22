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
constexpr Rect Maneuver{0, 0, 85, 140};
constexpr Rect Speed{85, 0, 80, 140};
constexpr Rect Limits{165, 0, 60, 140};
constexpr Rect Alerts{225, 0, 95, 140};
constexpr Rect Street{0, 140, 320, 30};
constexpr Rect Full{0, 0, 320, 170};
constexpr int MaxRegionPixels = Alerts.width * Alerts.height;
}  // namespace layout

}  // namespace waze_hud
